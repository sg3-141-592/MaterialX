//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifdef MATERIALX_BUILD_RENDER_HYDRA

#include <MaterialXGraphEditor/HydraRenderPipeline.h>

#include <MaterialXCore/Material.h>
#include <MaterialXGenShader/ShaderTranslator.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXRender/CgltfLoader.h>
#include <MaterialXRender/GeometryHandler.h>
#include <MaterialXRender/Mesh.h>
#include <MaterialXRender/StbImageLoader.h>
#include <MaterialXRender/TinyObjLoader.h>
#include <MaterialXRenderGlsl/GLTextureHandler.h>
#include <MaterialXRenderGlsl/External/Glad/glad.h>
#include <MaterialXFormat/File.h>
#include <MaterialXFormat/Util.h>

#include <pxr/pxr.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/half.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgiGL/hgi.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

const double MATERIAL_SETTLE_SECONDS = 0.20;
const int MAX_PREVIEW_SAMPLES = 64;

double currentTimeSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string hydraPluginPath()
{
    std::vector<std::string> paths;

    const mx::FilePath moduleDir = mx::FilePath::getModulePath();
    const mx::FilePath localHydra = moduleDir / "hydra";
    if ((localHydra / "plugInfo.json").exists())
    {
        paths.push_back(localHydra.asString());
    }
#ifdef MATERIALX_HYDRA_PLUGIN_DIR
    else
    {
        paths.push_back(MATERIALX_HYDRA_PLUGIN_DIR);
    }
#endif

#ifdef MATERIALX_HYDRA_USD_PLUGIN_DIR
    mx::FilePath usdPluginRoot(MATERIALX_HYDRA_USD_PLUGIN_DIR);
    const mx::FilePath localUsd = moduleDir / "usd" / "lib" / "usd";
    if (localUsd.exists())
    {
        usdPluginRoot = localUsd;
    }
    bool foundResources = false;
    if (usdPluginRoot.exists())
    {
        const auto rootString = usdPluginRoot.asString();
        for (const auto& entry : std::filesystem::directory_iterator(rootString))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            const std::filesystem::path resources = entry.path() / "resources";
            if (std::filesystem::is_directory(resources))
            {
                paths.push_back(resources.string());
                foundResources = true;
            }
        }
    }
    const std::filesystem::path usdPluginRootFs(usdPluginRoot.asString());
    const std::filesystem::path usdInstallRoot =
        usdPluginRootFs.parent_path().parent_path();
    const std::filesystem::path extraUsdPlugins = usdInstallRoot / "plugin" / "usd";
    if (std::filesystem::is_directory(extraUsdPlugins))
    {
        for (const auto& entry : std::filesystem::directory_iterator(extraUsdPlugins))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            const std::filesystem::path resources = entry.path() / "resources";
            if (std::filesystem::is_directory(resources))
            {
                paths.push_back(resources.string());
                foundResources = true;
            }
        }
    }
    if (!foundResources)
    {
        paths.push_back(MATERIALX_HYDRA_USD_PLUGIN_DIR);
    }
#endif

    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    std::string result;
    for (const std::string& path : paths)
    {
        if (!result.empty())
        {
            result += separator;
        }
        result += path;
    }
    return result;
}

void configureHydraEnvironment()
{
    static bool configured = false;
    if (configured)
    {
        return;
    }
    configured = true;

    std::string pluginPath = hydraPluginPath();
    if (const char* existing = getenv("PXR_PLUGINPATH_NAME"))
    {
        if (!pluginPath.empty())
        {
#ifdef _WIN32
            pluginPath += ";";
#else
            pluginPath += ":";
#endif
        }
        pluginPath += existing;
    }
    if (!pluginPath.empty())
    {
#ifdef _WIN32
        _putenv_s("PXR_PLUGINPATH_NAME", pluginPath.c_str());
#else
        setenv("PXR_PLUGINPATH_NAME", pluginPath.c_str(), 1);
#endif
    }
    const mx::FilePath moduleDir = mx::FilePath::getModulePath();
    const mx::FilePath localUsd = moduleDir / "usd" / "lib" / "usd";
    const mx::FilePath localHydra = moduleDir / "hydra";
    if (localUsd.exists())
    {
        PlugRegistry::GetInstance().RegisterPlugins(localUsd.asString());
        const mx::FilePath localPluginUsd = moduleDir / "usd" / "plugin" / "usd";
        if (localPluginUsd.exists())
        {
            PlugRegistry::GetInstance().RegisterPlugins(localPluginUsd.asString());
        }
    }
    if (localHydra.exists())
    {
        PlugRegistry::GetInstance().RegisterPlugins(localHydra.asString());
    }
#ifdef MATERIALX_HYDRA_USD_PLUGIN_DIR
    PlugRegistry::GetInstance().RegisterPlugins(MATERIALX_HYDRA_USD_PLUGIN_DIR);
    const mx::FilePath buildPluginUsd =
        mx::FilePath(MATERIALX_HYDRA_USD_PLUGIN_DIR) / ".." / ".." / "plugin" / "usd";
    if (buildPluginUsd.exists())
    {
        PlugRegistry::GetInstance().RegisterPlugins(buildPluginUsd.asString());
    }
#endif
#ifdef MATERIALX_HYDRA_PLUGIN_DIR
    PlugRegistry::GetInstance().RegisterPlugins(MATERIALX_HYDRA_PLUGIN_DIR);
#endif

#ifdef MATERIALX_HYDRA_MTLX_LIBRARY_PATH
    if (getenv("PXR_MTLX_STDLIB_SEARCH_PATHS") == nullptr)
    {
#ifdef _WIN32
        _putenv_s("PXR_MTLX_STDLIB_SEARCH_PATHS", MATERIALX_HYDRA_MTLX_LIBRARY_PATH);
#else
        setenv("PXR_MTLX_STDLIB_SEARCH_PATHS", MATERIALX_HYDRA_MTLX_LIBRARY_PATH, 0);
#endif
    }
#endif
}

GfVec3f toGfVec3f(const mx::Vector3& v)
{
    return GfVec3f(v[0], v[1], v[2]);
}

GfVec3f toGfVec3f(const mx::Color3& c)
{
    return GfVec3f(c[0], c[1], c[2]);
}

GfVec4f toGfVec4f(const mx::Vector4& v)
{
    return GfVec4f(v[0], v[1], v[2], v[3]);
}

GfVec4f toGfVec4f(const mx::Color4& c)
{
    return GfVec4f(c[0], c[1], c[2], c[3]);
}

SdfValueTypeName usdTypeForMtlxType(const std::string& type, const std::string& name)
{
    if (type == "float")
    {
        return SdfValueTypeNames->Float;
    }
    if (type == "integer")
    {
        return SdfValueTypeNames->Int;
    }
    if (type == "boolean")
    {
        return SdfValueTypeNames->Bool;
    }
    if (type == "color3")
    {
        return SdfValueTypeNames->Color3f;
    }
    if (type == "color4")
    {
        return SdfValueTypeNames->Color4f;
    }
    if (type == "vector3")
    {
        return name == "normal" ? SdfValueTypeNames->Normal3f : SdfValueTypeNames->Vector3f;
    }
    if (type == "vector4")
    {
        return SdfValueTypeNames->Float4;
    }
    return SdfValueTypeNames->String;
}

bool setUsdInputValue(UsdShadeShader& shader,
                      const std::string& name,
                      const std::string& type,
                      const mx::ValuePtr& value)
{
    if (!value)
    {
        return false;
    }

    UsdShadeInput input = shader.CreateInput(TfToken(name), usdTypeForMtlxType(type, name));
    if (type == "float" && value->isA<float>())
    {
        return input.Set(value->asA<float>());
    }
    if (type == "integer" && value->isA<int>())
    {
        return input.Set(value->asA<int>());
    }
    if (type == "boolean" && value->isA<bool>())
    {
        return input.Set(value->asA<bool>());
    }
    if (type == "color3" && value->isA<mx::Color3>())
    {
        return input.Set(toGfVec3f(value->asA<mx::Color3>()));
    }
    if (type == "vector3" && value->isA<mx::Vector3>())
    {
        return input.Set(toGfVec3f(value->asA<mx::Vector3>()));
    }
    if (type == "color4" && value->isA<mx::Color4>())
    {
        return input.Set(toGfVec4f(value->asA<mx::Color4>()));
    }
    if (type == "vector4" && value->isA<mx::Vector4>())
    {
        return input.Set(toGfVec4f(value->asA<mx::Vector4>()));
    }
    if (value->isA<std::string>())
    {
        return input.Set(value->asA<std::string>());
    }
    return false;
}

mx::NodePtr findUsdPreviewSurfaceNode(const mx::DocumentPtr& doc)
{
    if (!doc)
    {
        return nullptr;
    }
    for (mx::ElementPtr elem : doc->traverseTree())
    {
        mx::NodePtr node = elem->asA<mx::Node>();
        if (node && node->getCategory() == "UsdPreviewSurface")
        {
            return node;
        }
    }
    return nullptr;
}

mx::NodePtr findSurfaceShaderNode(const mx::DocumentPtr& doc,
                                  const mx::TypedElementPtr& element)
{
    if (!doc)
    {
        return nullptr;
    }

    mx::TypedElementPtr target = element;
    if (!target)
    {
        std::vector<mx::TypedElementPtr> elements = mx::findRenderableElements(doc);
        if (!elements.empty())
        {
            target = elements[0];
        }
    }

    mx::NodePtr node = target ? target->asA<mx::Node>() : nullptr;
    if (!node)
    {
        return nullptr;
    }
    if (node->getType() == "material")
    {
        std::vector<mx::NodePtr> shaders = mx::getShaderNodes(node);
        return shaders.empty() ? nullptr : shaders[0];
    }
    return node;
}

void authorPreviewSurfaceFromShader(const mx::NodePtr& shader,
                                    UsdShadeShader& previewShader)
{
    struct PreviewInput
    {
        const char* mtlxName;
        const char* usdName;
        const char* usdType;
    };
    static const PreviewInput inputs[] = {
        { "base_color", "diffuseColor", "color3" },
        { "metalness", "metallic", "float" },
        { "specular_roughness", "roughness", "float" },
        { "opacity", "opacity", "float" },
        { "coat", "clearcoat", "float" },
        { "coat_roughness", "clearcoatRoughness", "float" },
        { "ior", "ior", "float" },
    };

    if (shader)
    {
        for (const PreviewInput& mapping : inputs)
        {
            mx::InputPtr input = shader->getInput(mapping.mtlxName);
            if (!input || input->getConnectedNode())
            {
                continue;
            }
            setUsdInputValue(previewShader, mapping.usdName, mapping.usdType, input->getValue());
        }
    }

    // Supply stable preview defaults for inputs whose MaterialX source is a
    // graph or is otherwise not representable as a constant UsdPreviewSurface
    // value yet.
    auto ensureInput = [&](const TfToken& name, const SdfValueTypeName& type, const VtValue& value)
    {
        UsdShadeInput input = previewShader.GetInput(name);
        if (!input)
        {
            previewShader.CreateInput(name, type).Set(value);
        }
    };
    ensureInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f, VtValue(GfVec3f(0.8f)));
    ensureInput(TfToken("roughness"), SdfValueTypeNames->Float, VtValue(0.2f));
    ensureInput(TfToken("metallic"), SdfValueTypeNames->Float, VtValue(0.0f));
    ensureInput(TfToken("opacity"), SdfValueTypeNames->Float, VtValue(1.0f));
    ensureInput(TfToken("ior"), SdfValueTypeNames->Float, VtValue(1.5f));
}

} // anonymous namespace

struct HydraRenderPipeline::Impl
{
    mx::DocumentPtr document;
    mx::DocumentPtr stdLib;
    mx::FileSearchPath searchPath;
    mx::FilePath meshFilename;
    mx::FilePath envRadianceFilename;
    mx::TypedElementPtr renderElement;

    std::string delegateId;
    mx::GeometryHandlerPtr geometryHandler;
    mx::ImageHandlerPtr imageHandler;
    mx::ImagePtr image;
    mx::CameraPtr viewCamera;
    RenderCameraState cameraState;
    unsigned int cameraRevision = 0;

    RenderSettings settings;
    int viewWidth = 0;
    int viewHeight = 0;
    unsigned int textureID = 0;

    bool initialized = false;
    bool stageDirty = true;
    bool materialDirty = false;
    bool materialImmediate = true;
    bool cameraDirty = true;
    bool captureRequested = false;
    bool active = false;
    double materialDirtyTime = 0.0;
    double captureRequestTime = 0.0;
    int framesSinceCapture = 0;
    int sessionWidth = 0;
    int sessionHeight = 0;
    mx::FilePath captureFilename;

    UsdStageRefPtr stage;
    UsdGeomCamera camera;
    UsdGeomMesh mesh;
    UsdShadeMaterial material;
    UsdLuxDomeLight domeLight;
    SdfPath cameraPath;

    std::unique_ptr<UsdImagingDelegate> sceneDelegate;
    HdPluginRenderDelegateUniqueHandle renderDelegate;
    std::unique_ptr<HdRenderIndex> renderIndex;
    std::unique_ptr<HdxTaskController> taskController;
    std::unique_ptr<HdEngine> engine;
    std::unique_ptr<HgiGL> hgi;
    HdDriver hgiDriver;
    HdTaskSharedPtrVector tasks;

    void initialize();
    void loadGeometry();
    void buildStage();
    void updateCamera();
    void updateSettings();
    void applyRenderSize();
    void rebuildHost();
    void uploadFrame();
    void saveCapture();
    void destroyHost();

};

void HydraRenderPipeline::Impl::loadGeometry()
{
    geometryHandler = mx::GeometryHandler::create();
    geometryHandler->addLoader(mx::TinyObjLoader::create());
    geometryHandler->addLoader(mx::CgltfLoader::create());

    mx::FileSearchPath localSearchPath = searchPath;
    localSearchPath.append(mx::FilePath::getCurrentPath());
    mx::FilePath resolvedMesh = localSearchPath.find(meshFilename);
    if (!resolvedMesh.isEmpty())
    {
        geometryHandler->loadGeometry(resolvedMesh);
    }
}

void HydraRenderPipeline::Impl::updateCamera()
{
    if (!camera)
    {
        return;
    }

    mx::Vector3 eye = cameraState.target;
    {
        mx::Matrix44 invArcball = viewCamera ? viewCamera->arcballMatrix().getInverse() : mx::Matrix44();
        mx::Vector3 offset = invArcball.transformVector(cameraState.position - cameraState.target);
        offset = offset * (1.0f / std::max(cameraState.zoom, 0.001f));
        eye = cameraState.target + offset;
    }

    mx::Vector3 target = cameraState.target + cameraState.userTranslation;
    mx::Vector3 forward = (target - eye).getNormalized();
    mx::Vector3 up = cameraState.up;
    if (viewCamera)
    {
        up = viewCamera->arcballMatrix().getInverse().transformVector(cameraState.up).getNormalized();
    }
    mx::Vector3 right = forward.cross(up).getNormalized();
    mx::Vector3 camUp = right.cross(forward).getNormalized();

    GfMatrix4d transform(1.0);
    transform.SetColumn(0, GfVec4d(right[0], right[1], right[2], 0.0));
    transform.SetColumn(1, GfVec4d(camUp[0], camUp[1], camUp[2], 0.0));
    transform.SetColumn(2, GfVec4d(-forward[0], -forward[1], -forward[2], 0.0));
    transform.SetColumn(3, GfVec4d(eye[0], eye[1], eye[2], 1.0));

    const float pi = std::acos(-1.0f);
    const float aperture = 36.0f;
    const float focalLength = (aperture * 0.5f) / std::tan(cameraState.viewAngle * pi / 360.0f);
    const float aspect = viewHeight > 0 ? (float) viewWidth / (float) viewHeight : 1.0f;

    GfCamera gfCamera;
    gfCamera.SetTransform(transform);
    gfCamera.SetFocalLength(focalLength);
    gfCamera.SetHorizontalAperture(aperture);
    gfCamera.SetVerticalAperture(aperture / std::max(aspect, 0.001f));
    gfCamera.SetClippingRange(GfRange1f(0.05f, 5000.0f));
    camera.SetFromCamera(gfCamera, UsdTimeCode::Default());
    cameraDirty = false;
}

void HydraRenderPipeline::Impl::buildStage()
{
    configureHydraEnvironment();

    stage = UsdStage::CreateInMemory();
    if (!stage)
    {
        return;
    }

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);

    // Material
    material = UsdShadeMaterial::Define(stage, SdfPath("/Model/Material"));
    UsdShadeShader previewShader = UsdShadeShader::Define(stage, SdfPath("/Model/Material/PreviewSurface"));
    previewShader.CreateIdAttr(VtValue(TfToken("ND_UsdPreviewSurface_surfaceshader")));

    mx::NodePtr previewNode;
    mx::DocumentPtr translated = document ? document->copy() : nullptr;
    try
    {
        if (translated)
        {
            mx::ShaderTranslator::create()->translateAllMaterials(translated, "UsdPreviewSurface");
            previewNode = findUsdPreviewSurfaceNode(translated);
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Hydra: MaterialX to UsdPreviewSurface translation failed: " << e.what() << std::endl;
    }

    mx::NodePtr sourceShader = findSurfaceShaderNode(document, renderElement);
    if (sourceShader)
    {
        authorPreviewSurfaceFromShader(sourceShader, previewShader);
    }
    else if (previewNode)
    {
        for (mx::InputPtr input : previewNode->getInputs())
        {
            if (!input->getConnectedNode())
            {
                setUsdInputValue(previewShader, input->getName(), input->getType(), input->getValue());
            }
        }
    }
    else
    {
        authorPreviewSurfaceFromShader(nullptr, previewShader);
    }

    UsdShadeOutput surface = previewShader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(surface);
    material.CreateSurfaceOutput(TfToken("cycles")).ConnectToSource(surface);

    // Geometry: use the first mesh partition. This matches the current preview
    // use case and can be extended to geom subsets later.
    if (geometryHandler && !geometryHandler->getMeshes().empty())
    {
        mx::MeshPtr sourceMesh = geometryHandler->getMeshes()[0];
        if (sourceMesh && sourceMesh->getPartitionCount() > 0)
        {
            mx::MeshPartitionPtr partition = sourceMesh->getPartition(0);
            mesh = UsdGeomMesh::Define(stage, SdfPath("/Model/Mesh"));

            VtVec3fArray points;
            mx::MeshStreamPtr positionStream = sourceMesh->getStream(mx::MeshStream::POSITION_ATTRIBUTE, 0);
            if (positionStream)
            {
                points.reserve(positionStream->getSize());
                for (size_t i = 0; i < positionStream->getSize(); ++i)
                {
                    const mx::Vector3& p = positionStream->getElement<mx::Vector3>(i);
                    points.push_back(GfVec3f(p[0], p[1], p[2]));
                }
            }
            mesh.CreatePointsAttr().Set(points);

            VtIntArray counts;
            counts.resize(partition->getFaceCount(), 3);
            mesh.CreateFaceVertexCountsAttr().Set(counts);

            VtIntArray indices;
            indices.reserve(partition->getIndices().size());
            for (uint32_t index : partition->getIndices())
            {
                indices.push_back(static_cast<int>(index));
            }
            mesh.CreateFaceVertexIndicesAttr().Set(indices);

            mx::MeshStreamPtr normalStream = sourceMesh->getStream(mx::MeshStream::NORMAL_ATTRIBUTE, 0);
            if (normalStream)
            {
                VtVec3fArray normals;
                normals.reserve(normalStream->getSize());
                for (size_t i = 0; i < normalStream->getSize(); ++i)
                {
                    const mx::Vector3& n = normalStream->getElement<mx::Vector3>(i);
                    normals.push_back(GfVec3f(n[0], n[1], n[2]));
                }
                mesh.CreateNormalsAttr().Set(normals);
                mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
            }

            mx::MeshStreamPtr texcoordStream = sourceMesh->getStream(mx::MeshStream::TEXCOORD_ATTRIBUTE, 0);
            if (texcoordStream)
            {
                VtVec2fArray texcoords;
                texcoords.reserve(texcoordStream->getSize());
                for (size_t i = 0; i < texcoordStream->getSize(); ++i)
                {
                    const mx::Vector2& uv = texcoordStream->getElement<mx::Vector2>(i);
                    texcoords.push_back(GfVec2f(uv[0], uv[1]));
                }
                UsdGeomPrimvarsAPI primvars(mesh.GetPrim());
                UsdGeomPrimvar st = primvars.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
                st.Set(texcoords);
            }

            mesh.CreateExtentAttr().Set(VtVec3fArray{
                GfVec3f(sourceMesh->getMinimumBounds()[0], sourceMesh->getMinimumBounds()[1], sourceMesh->getMinimumBounds()[2]),
                GfVec3f(sourceMesh->getMaximumBounds()[0], sourceMesh->getMaximumBounds()[1], sourceMesh->getMaximumBounds()[2])});

            UsdShadeMaterialBindingAPI binding = UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim());
            binding.Bind(material);
        }
    }

    // Camera
    cameraPath = SdfPath("/Camera");
    camera = UsdGeomCamera::Define(stage, cameraPath);
    updateCamera();

    // Environment light. Use the MaterialX environment map when available.
    domeLight = UsdLuxDomeLight::Define(stage, SdfPath("/DomeLight"));
    domeLight.CreateIntensityAttr().Set(settings.lightIntensity);
    domeLight.CreateColorAttr().Set(GfVec3f(1.0f));
    if (!envRadianceFilename.isEmpty())
    {
        mx::FileSearchPath localSearchPath = searchPath;
        localSearchPath.append(mx::FilePath::getCurrentPath());
        mx::FilePath resolved = localSearchPath.find(envRadianceFilename);
        if (!resolved.isEmpty() && resolved.exists())
        {
            domeLight.CreateTextureFileAttr().Set(SdfAssetPath(resolved.asString()));
        }
    }

    rebuildHost();
    stageDirty = false;
}

void HydraRenderPipeline::Impl::destroyHost()
{
    tasks.clear();
    taskController.reset();
    sceneDelegate.reset();
    engine.reset();
    renderIndex.reset();
    renderDelegate = nullptr;
    hgi.reset();
}

void HydraRenderPipeline::Impl::rebuildHost()
{
    destroyHost();
    configureHydraEnvironment();

    if (!stage)
    {
        return;
    }

    HdRendererPluginRegistry& registry = HdRendererPluginRegistry::GetInstance();
    HdRenderSettingsMap settingsMap;
    // Keep the stage at the units authored above; the default Cycles plugin
    // assumption of centimeters would otherwise change light falloff.
    settingsMap[TfToken("stageMetersPerUnit")] = VtValue(1.0f);
    settingsMap[TfToken("cycles:samples")] = VtValue(std::min(settings.maxSamples, MAX_PREVIEW_SAMPLES));

    renderDelegate = registry.CreateRenderDelegate(TfToken(delegateId), settingsMap);
    if (!renderDelegate)
    {
        std::cerr << "Hydra: failed to create the '" << delegateId << "' render delegate" << std::endl;
        return;
    }

    hgi = std::make_unique<HgiGL>();
    hgiDriver.name = HgiTokens->renderDriver;
    hgiDriver.driver = VtValue(static_cast<Hgi*>(hgi.get()));
    HdDriverVector drivers = { &hgiDriver };
    renderIndex.reset(HdRenderIndex::New(renderDelegate.Get(), drivers));
    if (!renderIndex)
    {
        std::cerr << "Hydra: failed to create the render index" << std::endl;
        destroyHost();
        return;
    }

    sceneDelegate = std::make_unique<UsdImagingDelegate>(renderIndex.get(), SdfPath::AbsoluteRootPath());
    sceneDelegate->Populate(stage->GetPseudoRoot());
    sceneDelegate->SetTime(UsdTimeCode::Default());
    sceneDelegate->SyncAll(true);

    taskController = std::make_unique<HdxTaskController>(renderIndex.get(), SdfPath("/task_controller"));
    taskController->SetEnablePresentation(false);
    taskController->SetEnableSelection(false);
    taskController->SetRenderOutputs({HdAovTokens->color});
    taskController->SetRenderTags({HdRenderTagTokens->geometry});

    HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
    collection.SetRootPath(SdfPath::AbsoluteRootPath());
    taskController->SetCollection(collection);
    taskController->SetCameraPath(cameraPath);
    applyRenderSize();

    engine = std::make_unique<HdEngine>();
    engine->SetTaskContextData(HdxTokens->selectionState,
                               VtValue(HdxSelectionTrackerSharedPtr(new HdxSelectionTracker())));
}

void HydraRenderPipeline::Impl::applyRenderSize()
{
    if (!taskController)
    {
        return;
    }
    const int width = std::max(viewWidth, 1);
    const int height = std::max(viewHeight, 1);
    taskController->SetRenderBufferSize(GfVec2i(width, height));
    taskController->SetRenderViewport(GfVec4d(0.0, 0.0, width, height));
    GfRange2f displayWindow(GfVec2f(0.0f, 0.0f), GfVec2f((float) width, (float) height));
    GfRect2i dataWindow(GfVec2i(0, 0), width, height);
    taskController->SetFraming(CameraUtilFraming(displayWindow, dataWindow));
    sessionWidth = width;
    sessionHeight = height;
}

void HydraRenderPipeline::Impl::updateSettings()
{
    if (!renderDelegate)
    {
        return;
    }
    renderDelegate->SetRenderSetting(TfToken("cycles:samples"),
                                     VtValue(std::min(settings.maxSamples, MAX_PREVIEW_SAMPLES)));
    if (domeLight)
    {
        domeLight.CreateIntensityAttr().Set(settings.lightIntensity);
    }
}

void HydraRenderPipeline::Impl::uploadFrame()
{
    if (!taskController || !imageHandler)
    {
        return;
    }

    HdRenderBuffer* renderBuffer = taskController->GetRenderOutput(HdAovTokens->color);
    if (!renderBuffer)
    {
        return;
    }
    renderBuffer->Resolve();

    const int width = static_cast<int>(renderBuffer->GetWidth());
    const int height = static_cast<int>(renderBuffer->GetHeight());
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const HdFormat format = renderBuffer->GetFormat();
    if (format != HdFormatFloat32Vec4 && format != HdFormatFloat16Vec4)
    {
        std::cerr << "Hydra: unsupported color AOV format " << format << std::endl;
        return;
    }

    if (!image || (int) image->getWidth() != width || (int) image->getHeight() != height)
    {
        if (image)
        {
            imageHandler->releaseRenderResources(image);
        }
        image = mx::Image::create(width, height, 4, mx::Image::BaseType::FLOAT);
        image->createResourceBuffer();
    }

    const void* source = renderBuffer->Map();
    if (!source)
    {
        return;
    }

    float* destination = static_cast<float*>(image->getResourceBuffer());
    const size_t count = (size_t) width * (size_t) height * 4;
    if (format == HdFormatFloat32Vec4)
    {
        std::memcpy(destination, source, count * sizeof(float));
    }
    else
    {
        const GfHalf* halfSource = static_cast<const GfHalf*>(source);
        for (size_t i = 0; i < count; ++i)
        {
            destination[i] = float(halfSource[i]);
        }
    }
    renderBuffer->Unmap();

    if (imageHandler->createRenderResources(image, false))
    {
        textureID = image->getResourceId();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void HydraRenderPipeline::Impl::saveCapture()
{
    if (!image || !imageHandler)
    {
        return;
    }

    mx::ImagePtr saveImage = mx::Image::create(image->getWidth(), image->getHeight(), 4, mx::Image::BaseType::UINT8);
    saveImage->createResourceBuffer();
    for (unsigned int y = 0; y < image->getHeight(); ++y)
    {
        for (unsigned int x = 0; x < image->getWidth(); ++x)
        {
            mx::Color4 color = image->getTexelColor(x, y);
            for (int c = 0; c < 4; ++c)
            {
                color[c] = std::isfinite(color[c]) ? std::clamp(color[c], 0.0f, 1.0f) : 0.0f;
            }
            mx::Color3 srgb = mx::Color3(color[0], color[1], color[2]).linearToSrgb();
            color[0] = srgb[0];
            color[1] = srgb[1];
            color[2] = srgb[2];
            saveImage->setTexelColor(x, y, color);
        }
    }

    if (imageHandler->saveImage(captureFilename, saveImage, true))
    {
        std::cout << "Wrote frame to disk: " << captureFilename.asString() << std::endl;
    }
    else
    {
        std::cerr << "Failed to write frame to disk: " << captureFilename.asString() << std::endl;
    }
    captureRequested = false;
}

void HydraRenderPipeline::Impl::initialize()
{
    if (initialized)
    {
        return;
    }
    initialized = true;
    configureHydraEnvironment();
    imageHandler = mx::GLTextureHandler::create(mx::StbImageLoader::create());
    imageHandler->setSearchPath(searchPath);
    loadGeometry();
    stageDirty = true;
}

HydraRenderPipeline::HydraRenderPipeline(mx::DocumentPtr doc,
                                         mx::DocumentPtr stdLib,
                                         const std::string& meshFilename,
                                         const std::string& envRadianceFilename,
                                         const mx::FileSearchPath& searchPath,
                                         int viewWidth,
                                         int viewHeight,
                                         const std::string& delegateId,
                                         const std::string& /*renderPass*/) :
    _impl(std::make_unique<Impl>()),
    _displayName("Hydra (" + delegateId + ")")
{
    _impl->document = doc;
    _impl->stdLib = stdLib;
    _impl->meshFilename = meshFilename;
    _impl->envRadianceFilename = envRadianceFilename;
    _impl->searchPath = searchPath;
    _impl->viewWidth = viewWidth;
    _impl->viewHeight = viewHeight;
    _impl->viewCamera = mx::Camera::create();
    _impl->delegateId = delegateId == "Cycles" ? "HdCyclesPlugin" :
                       (delegateId == "GL" ? "HdStormRendererPlugin" : delegateId);
    _impl->cameraState.position = mx::Vector3(0.0f, 0.0f, 5.0f);
    _impl->cameraState.target = mx::Vector3(0.0f);
    _impl->cameraState.up = mx::Vector3(0.0f, 1.0f, 0.0f);
    _impl->cameraState.viewAngle = 45.0f;
    _impl->cameraState.zoom = 1.0f;
}

void HydraRenderPipeline::configureEnvironment()
{
    configureHydraEnvironment();
}

bool HydraRenderPipeline::isDelegateAvailable(const std::string& delegateName)
{
    configureHydraEnvironment();
    TfToken id(delegateName == "Cycles" ? "HdCyclesPlugin" :
               (delegateName == "GL" ? "HdStormRendererPlugin" : delegateName));
    return HdRendererPluginRegistry::GetInstance().IsRegisteredPlugin(id);
}

HydraRenderPipeline::~HydraRenderPipeline()
{
    if (_impl->imageHandler && _impl->image)
    {
        _impl->imageHandler->releaseRenderResources(_impl->image);
    }
    _impl->destroyHost();
}

RenderCapabilities HydraRenderPipeline::getCapabilities() const
{
    RenderCapabilities caps;
    caps.passes = { { "combined", "Combined" } };
    caps.meshReload = true;
    caps.asynchronous = true;
    return caps;
}

void HydraRenderPipeline::initialize()
{
    _impl->initialize();
}

void HydraRenderPipeline::setDocument(mx::DocumentPtr document)
{
    _impl->document = document;
    _impl->stageDirty = true;
    _impl->materialDirty = false;
}

void HydraRenderPipeline::updateMaterials(mx::TypedElementPtr typedElem)
{
    _impl->renderElement = typedElem;
    _impl->stageDirty = true;
    _impl->materialDirty = false;
}

void HydraRenderPipeline::loadMesh(const mx::FilePath& filename)
{
    _impl->meshFilename = filename;
    _impl->loadGeometry();
    _impl->stageDirty = true;
}

void HydraRenderPipeline::setSettings(const RenderSettings& settings)
{
    _impl->settings = settings;
    _impl->updateSettings();
}

void HydraRenderPipeline::resize(int width, int height)
{
    _impl->viewWidth = width;
    _impl->viewHeight = height;
    _impl->cameraDirty = true;
}

void HydraRenderPipeline::setCamera(mx::CameraPtr camera, const RenderCameraState& state)
{
    if (state.revision != _impl->cameraRevision)
    {
        _impl->cameraRevision = state.revision;
        _impl->cameraDirty = true;
    }
    _impl->viewCamera = camera;
    _impl->cameraState = state;
}

void HydraRenderPipeline::render()
{
    _impl->initialize();

    if (_impl->materialDirty &&
        (_impl->materialImmediate || currentTimeSeconds() - _impl->materialDirtyTime >= MATERIAL_SETTLE_SECONDS))
    {
        _impl->materialDirty = false;
        _impl->materialImmediate = false;
        _impl->stageDirty = true;
    }

    if (_impl->stageDirty)
    {
        _impl->buildStage();
    }
    else
    {
        if (_impl->cameraDirty && _impl->camera)
        {
            _impl->updateCamera();
            if (_impl->sceneDelegate)
            {
                _impl->sceneDelegate->ApplyPendingUpdates();
                _impl->sceneDelegate->SyncAll(true);
            }
        }
        if (_impl->taskController &&
            (_impl->viewWidth != _impl->sessionWidth || _impl->viewHeight != _impl->sessionHeight))
        {
            _impl->applyRenderSize();
        }
    }

    if (!_impl->engine || !_impl->renderIndex || !_impl->taskController)
    {
        return;
    }

    _impl->tasks = _impl->taskController->GetRenderingTasks();
    _impl->engine->Execute(_impl->renderIndex.get(), &_impl->tasks);
    _impl->uploadFrame();

    if (_impl->captureRequested && _impl->image)
    {
        ++_impl->framesSinceCapture;
        const double now = currentTimeSeconds();
        if ((_impl->taskController->IsConverged() || _impl->framesSinceCapture > 4) &&
            now - _impl->captureRequestTime >= 0.5)
        {
            _impl->saveCapture();
        }
    }
}

unsigned int HydraRenderPipeline::getColorTexture() const
{
    return _impl->textureID;
}

void HydraRenderPipeline::requestFrameCapture(const mx::FilePath& filename)
{
    _impl->captureRequested = true;
    _impl->captureFilename = filename;
    _impl->captureRequestTime = currentTimeSeconds();
    _impl->framesSinceCapture = 0;
}

bool HydraRenderPipeline::isFrameCaptureComplete() const
{
    return !_impl->captureRequested;
}

bool HydraRenderPipeline::isNodeDefSupported(const mx::NodeDefPtr& /*nodeDef*/)
{
    return true;
}

void HydraRenderPipeline::modifyUniform(const std::string& /*name*/, mx::ValuePtr /*value*/)
{
    _impl->materialDirty = true;
    _impl->materialImmediate = false;
    _impl->materialDirtyTime = currentTimeSeconds();
}

void HydraRenderPipeline::setActive(bool active)
{
    _impl->active = active;
}

mx::ImageHandlerPtr HydraRenderPipeline::getImageHandler() const
{
    return _impl->imageHandler;
}

const mx::StringSet& HydraRenderPipeline::getXincludeFiles() const
{
    static const mx::StringSet empty;
    return empty;
}

#endif // MATERIALX_BUILD_RENDER_HYDRA
