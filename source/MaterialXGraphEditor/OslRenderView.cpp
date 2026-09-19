//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifdef MATERIALX_BUILD_GEN_OSL

#include <MaterialXGraphEditor/OslRenderView.h>

#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#ifdef MATERIALX_BUILD_OCIO
#include <MaterialXGenShader/OcioColorManagementSystem.h>
#endif
#include <MaterialXGenShader/Util.h>

#include <MaterialXRenderGlsl/GLTextureHandler.h>
#include <MaterialXRenderGlsl/GLUtil.h>

#include <MaterialXRender/OiioImageLoader.h>
#include <MaterialXRender/StbImageLoader.h>

#include <MaterialXFormat/Util.h>

#include <iostream>

//
// OslRenderView methods
//

OslRenderView::OslRenderView(mx::DocumentPtr doc,
                             mx::DocumentPtr stdLib,
                             const std::string& /*meshFilename*/,
                             const std::string& /*envRadianceFilename*/,
                             const mx::FileSearchPath& searchPath,
                             int viewWidth,
                             int viewHeight,
                             const std::string& oslCompilerExecutable,
                             const std::string& oslTestRenderExecutable,
                             const std::string& oslIncludePath,
                             const std::string& oslShaderPath) :
    _document(doc),
    _stdLib(stdLib),
    _searchPath(searchPath),
    _genContext(mx::OslShaderGenerator::create()),
    _oslRenderer(mx::OslRenderer::create()),
    _textureID(0),
    _renderWidth(0),
    _renderHeight(0),
    _renderDirty(true),
    _rendererInitialized(false),
    _captureRequested(false),
    _exitRequested(false)
{
    _viewWidth = viewWidth;
    _viewHeight = viewHeight;

    _viewCamera = mx::Camera::create();

    // Set default OSL generator options.
    _genContext.getOptions().targetColorSpaceOverride = "lin_rec709_scene";
    _genContext.getOptions().shaderInterfaceType = mx::SHADER_INTERFACE_COMPLETE;
    _genContext.getOptions().oslConnectCiWrapper = true;

    // Configure the OSL renderer executables.
    if (!oslCompilerExecutable.empty())
    {
        _oslRenderer->setOslCompilerExecutable(mx::FilePath(oslCompilerExecutable));
    }
    if (!oslTestRenderExecutable.empty())
    {
        _oslRenderer->setOslTestRenderExecutable(mx::FilePath(oslTestRenderExecutable));
    }
    if (!oslIncludePath.empty())
    {
        _oslRenderer->setOslIncludePath(mx::FileSearchPath(oslIncludePath));
    }

    // Generated OSL shaders include the MaterialX support headers (e.g.
    // mx_funcs.h), so ensure the genosl include folder is always on the OSL
    // compiler include path.
    mx::FilePath genOslIncludePath = searchPath.find("libraries/stdlib/genosl/include");
    if (!genOslIncludePath.isEmpty() && genOslIncludePath.exists())
    {
        mx::FileSearchPath includePaths(oslIncludePath);
        includePaths.append(genOslIncludePath);
        _oslRenderer->setOslIncludePath(includePaths);
    }

    if (!oslShaderPath.empty())
    {
        _oslRenderer->setOslShaderSearchPath(mx::FileSearchPath(oslShaderPath));
    }

    setDocument(doc);
}

void OslRenderView::initialize()
{
    // Initialize image handler.
    _imageHandler = mx::GLTextureHandler::create(mx::StbImageLoader::create());
#if MATERIALX_BUILD_OIIO
    _imageHandler->addLoader(mx::OiioImageLoader::create());
#endif
    _imageHandler->setSearchPath(_searchPath);
    _oslRenderer->setImageHandler(_imageHandler);

    // Initialize the OSL renderer.
    try
    {
        _oslRenderer->initialize();
        _rendererInitialized = true;
    }
    catch (mx::ExceptionRenderError& e)
    {
        for (const std::string& error : e.errorLog())
        {
            std::cerr << error << std::endl;
        }
        std::cerr << "OSL renderer not initialized: " << e.what() << std::endl;
    }
}

void OslRenderView::setDocument(mx::DocumentPtr document)
{
    _document = document;
    initContext(_genContext);
    _renderDirty = true;
}

void OslRenderView::initContext(mx::GenContext& context)
{
    // Initialize search path.
    context.registerSourceCodeSearchPath(_searchPath);

    // Initialize unit management.
    mx::UnitTypeDefPtr distanceTypeDef = _document->getUnitTypeDef("distance");
    mx::LinearUnitConverterPtr distanceConverter = mx::LinearUnitConverter::create(distanceTypeDef);
    mx::UnitConverterRegistryPtr unitRegistry = mx::UnitConverterRegistry::create();
    unitRegistry->addUnitConverter(distanceTypeDef, distanceConverter);
    mx::UnitTypeDefPtr angleTypeDef = _document->getUnitTypeDef("angle");
    mx::LinearUnitConverterPtr angleConverter = mx::LinearUnitConverter::create(angleTypeDef);
    unitRegistry->addUnitConverter(angleTypeDef, angleConverter);

    // Initialize color management.
    mx::ColorManagementSystemPtr cms;
#ifdef MATERIALX_BUILD_OCIO
    try
    {
        cms = mx::OcioColorManagementSystem::createFromBuiltinConfig(
            "ocio://studio-config-latest",
            context.getShaderGenerator().getTarget());
    }
    catch (const std::exception& /*e*/)
    {
        cms = mx::DefaultColorManagementSystem::create(context.getShaderGenerator().getTarget());
    }
#else
    cms = mx::DefaultColorManagementSystem::create(context.getShaderGenerator().getTarget());
#endif
    cms->loadLibrary(_document);
    context.getShaderGenerator().setColorManagementSystem(cms);

    // Initialize unit management.
    mx::UnitSystemPtr unitSystem = mx::UnitSystem::create(context.getShaderGenerator().getTarget());
    unitSystem->loadLibrary(_document);
    unitSystem->setUnitConverterRegistry(unitRegistry);
    context.getShaderGenerator().setUnitSystem(unitSystem);
    context.getOptions().targetDistanceUnit = "meter";

    // Register type definitions.
    context.getShaderGenerator().registerTypeDefs(_document);
}

void OslRenderView::updateMaterials(mx::TypedElementPtr typedElem)
{
    _genContext.clearUserData();

    try
    {
        if (!typedElem)
        {
            std::vector<mx::TypedElementPtr> elems = mx::findRenderableElements(_document);
            if (!elems.empty())
            {
                typedElem = elems[0];
            }
        }

        // Skip material nodes without upstream shaders.
        mx::NodePtr node = typedElem ? typedElem->asA<mx::Node>() : nullptr;
        if (node &&
            node->getCategory() == mx::SURFACE_MATERIAL_NODE_STRING &&
            mx::getShaderNodes(node).empty())
        {
            typedElem = nullptr;
        }

        if (typedElem)
        {
            // Generate the OSL shader for the selected element.
            _shaderName = typedElem->getNamePath();
            _shader = _genContext.getShaderGenerator().generate(_shaderName, typedElem, _genContext);

            // Determine the renderable output of the generated shader.
            const mx::ShaderStage& stage = _shader->getStage(mx::Stage::PIXEL);
            const mx::VariableBlock& outputs = stage.getOutputBlock(mx::OSL::OUTPUTS);
            if (!outputs.empty())
            {
                const mx::ShaderPort* output = outputs[0];
                const mx::TypeSyntax& typeSyntax = _genContext.getShaderGenerator().getSyntax().getTypeSyntax(output->getType());
                _shaderOutputName = output->getVariable();
                _shaderOutputType = typeSyntax.getTypeAlias().empty() ? typeSyntax.getName() : typeSyntax.getTypeAlias();
            }
            else
            {
                _shader = nullptr;
            }
        }
        else
        {
            _shader = nullptr;
        }

        _renderDirty = true;
    }
    catch (mx::Exception& e)
    {
        std::cerr << "Failed to generate OSL shader: " << e.what() << std::endl;
        _shader = nullptr;
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        _shader = nullptr;
    }
}

void OslRenderView::drawContents()
{
    // OSL renders are expensive, so only re-render when the shader or the
    // view size has changed since the last render.
    if (_renderDirty || _viewWidth != _renderWidth || _viewHeight != _renderHeight)
    {
        renderFrame();
        _renderDirty = false;
    }

    if (_captureRequested)
    {
        _captureRequested = false;
        if (_lastImage && _imageHandler->saveImage(_captureFilename, _lastImage, false))
        {
            std::cout << "Wrote frame to disk: " << _captureFilename.asString() << std::endl;
        }
    }

    if (_exitRequested)
    {
        _exitRequested = false;
    }
}

void OslRenderView::renderFrame()
{
    if (!_rendererInitialized || !_shader || _shaderOutputName.empty())
    {
        return;
    }

    // Set the size for the rendered image.
    _oslRenderer->setSize((unsigned int) _viewWidth, (unsigned int) _viewHeight);

    // Set the output path and shader name for the OSL pipeline.
    mx::FileSearchPath searchPath = mx::getDefaultDataSearchPath();
    mx::FilePath outputFilePath = searchPath.isEmpty() ? mx::FilePath() : searchPath[0];
    outputFilePath = outputFilePath / "OslRenderView";
    outputFilePath.createDirectory(true);
    _oslRenderer->setOslOutputFilePath(outputFilePath);
    _oslRenderer->setOslShaderName(_shaderName);
    _oslRenderer->setOslShaderOutput(_shaderOutputName, _shaderOutputType);

    // Set the scene template file for testrender.
    mx::FilePath sceneTemplatePath = searchPath.find("resources/Utilities/graph_editor_scene_template.xml");
    if (sceneTemplatePath.isEmpty() || !sceneTemplatePath.exists())
    {
        std::cerr << "OSL scene template file not found on the search path" << std::endl;
        return;
    }
    _oslRenderer->setOslTestRenderSceneTemplateFile(sceneTemplatePath.asString());

    // Set the search path for compiled utility shaders.
    mx::FilePath shaderPath = searchPath.find("resources/Utilities/");
    if (!shaderPath.isEmpty())
    {
        _oslRenderer->setOslUtilityOSOPath(shaderPath);
    }

    // Record the OSL data library path from the shader, if present.
    mx::ValuePtr osoPathValue = _shader->getAttribute("osoPath");
    if (osoPathValue)
    {
        _oslRenderer->setDataLibraryOSOPath(mx::FilePath(osoPathValue->getValueString()));
    }

    // Compile and render the shader, then upload the resulting image to an
    // OpenGL texture.
    double renderTimeMs = 0.0;
    bool renderSucceeded = false;
    try
    {
        _oslRenderer->createProgram(_shader);
        mx::ScopedTimer renderTimer;
        _oslRenderer->render();
        renderTimeMs = renderTimer.elapsedTime() * 1000.0;
        renderSucceeded = true;

        _lastImage = _oslRenderer->captureImage();
        if (_lastImage)
        {
            _textureID = _lastImage->getResourceId();
            _renderWidth = _viewWidth;
            _renderHeight = _viewHeight;
        }
    }
    catch (mx::ExceptionRenderError& e)
    {
        for (const std::string& error : e.errorLog())
        {
            std::cerr << error << std::endl;
        }
        std::cerr << e.what() << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    // The OSL pipeline uploads textures into the shared OpenGL context without
    // checking for errors. Drain any errors they generate here so that they
    // do not surface in another render backend (e.g. GLSL) after a backend
    // switch.
    mx::checkGlErrors("after OSL texture upload");

    if (renderSucceeded)
    {
        std::cout << "OSL testrender took " << renderTimeMs << " ms" << std::endl;
    }
}

void OslRenderView::loadMesh(const mx::FilePath& /*filename*/)
{
    // OSL renders are independent of mesh geometry.
}

bool OslRenderView::isNodeDefSupported(const mx::NodeDefPtr& nodeDef)
{
    if (!nodeDef)
    {
        return false;
    }
    return _genContext.getShaderGenerator().getImplementation(*nodeDef, _genContext) != nullptr;
}

void OslRenderView::modifyUniform(const std::string& name, mx::ValuePtr /*value*/)
{
    // Uniform editing through the OSL testrender pipeline is not yet supported.
    std::cout << "OSL backend: uniform modification is not yet supported (" << name << ")" << std::endl;
}

#endif // MATERIALX_BUILD_GEN_OSL