//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_OSLRENDERVIEW_H
#define MATERIALX_OSLRENDERVIEW_H

#ifdef MATERIALX_BUILD_GEN_OSL

#include <MaterialXGraphEditor/RenderViewBase.h>

#include <MaterialXGenOsl/OslShaderGenerator.h>
#include <MaterialXRenderOsl/OslRenderer.h>

#include <MaterialXRender/Timer.h>

namespace mx = MaterialX;

class OslRenderView;
using OslRenderViewPtr = std::shared_ptr<OslRenderView>;

/// @class OslRenderView
/// OSL render view embedded in the graph editor.
///
/// Renders the selected renderable element as an OSL shader, using the
/// file-based OSL pipeline (oslc compilation and testrender), and presents
/// the resulting image to the graph editor as an OpenGL texture.
class OslRenderView : public RenderViewBase
{
  public:
    OslRenderView(mx::DocumentPtr doc,
                  mx::DocumentPtr stdLib,
                  const std::string& meshFilename,
                  const std::string& envRadianceFilename,
                  const mx::FileSearchPath& searchPath,
                  int viewWidth,
                  int viewHeight,
                  const std::string& oslCompilerExecutable,
                  const std::string& oslTestRenderExecutable,
                  const std::string& oslIncludePath,
                  const std::string& oslShaderPath);
    ~OslRenderView() = default;

    // Return the name of this render backend.
    std::string getBackendName() const override
    {
        return "OSL";
    }

    // Initialize the viewer for rendering.
    void initialize() override;

    void setDocument(mx::DocumentPtr document) override;
    void updateMaterials(mx::TypedElementPtr typedElem) override;
    void loadMesh(const mx::FilePath& filename) override;
    void drawContents() override;

    // Return the OpenGL texture ID of the most recently rendered frame.
    unsigned int getRenderTextureId() const override
    {
        return _textureID;
    }

    // Return the camera used to view the rendered scene.
    mx::CameraPtr getViewCamera() override
    {
        return _viewCamera;
    }

    // Return the pixel ratio of the render view.
    float getPixelRatio() const override
    {
        return 1.0f;
    }

    // Camera interaction: left-drag orbits, right/shift-left-drag pans and
    // scroll or keypad +/- zoom. Camera changes trigger a debounced re-render.
    void setMouseButtonEvent(int button, bool down, mx::Vector2 pos) override;
    void setMouseMotionEvent(mx::Vector2 pos) override;
    void setKeyEvent(int key) override;
    void setScrollEvent(float scrollY) override;

    // Request a capture of the current frame, writing it to the given filename.
    void requestFrameCapture(const mx::FilePath& filename) override
    {
        _captureRequested = true;
        _captureFilename = filename;
    }

    // Request that the viewer be closed after the next frame is rendered.
    void requestExit() override
    {
        _exitRequested = true;
    }

    // Return the active image handler.
    mx::ImageHandlerPtr getImageHandler() const override
    {
        return _imageHandler;
    }

    // Return the set of files referenced by XInclude.
    const mx::StringSet& getXincludeFiles() const override
    {
        return _xincludeFiles;
    }

    // Return true if the given node definition is supported by the OSL backend.
    bool isNodeDefSupported(const mx::NodeDefPtr& nodeDef) override;

    // Update a uniform in the currently selected material.
    void modifyUniform(const std::string& name, mx::ValuePtr value) override;

  private:
    void initContext(mx::GenContext& context);
    void renderFrame();
    void markCameraDirty();
    mx::Vector3 computeCameraEye() const;
    mx::Vector3 computeCameraUp() const;

    // Document management
    mx::DocumentPtr _document;
    mx::DocumentPtr _stdLib;
    mx::FileSearchPath _searchPath;
    mx::StringSet _xincludeFiles;

    // Shader generator context
    mx::GenContext _genContext;

    // OSL renderer
    mx::OslRendererPtr _oslRenderer;

    // The generated OSL shader and its renderable output.
    mx::ShaderPtr _shader;
    std::string _shaderName;
    std::string _shaderOutputName;
    std::string _shaderOutputType;

    // Whether the constant background shader has been compiled for testrender.
    bool _backgroundShaderCompiled = false;

    // Resource handlers
    mx::ImageHandlerPtr _imageHandler;
    mx::CameraPtr _viewCamera;

    // Interactive camera state. The base camera matches the default scene
    // template and is orbited by the arcball stored in _viewCamera.
    mx::Vector3 _cameraPosition;
    mx::Vector3 _cameraTarget;
    mx::Vector3 _cameraUp;
    float _cameraFov;
    float _cameraZoom;
    mx::Vector3 _userTranslation;
    mx::Vector3 _userTranslationStart;
    bool _userTranslationActive;
    mx::Vector2 _userTranslationPixel;
    bool _cameraDirty;
    bool _cameraInteracting;
    double _lastCameraChangeTime;

    // Render state
    unsigned int _textureID;
    mx::ImagePtr _lastImage;
    int _renderWidth;
    int _renderHeight;
    bool _renderDirty;
    bool _rendererInitialized;

    // Frame capture
    bool _captureRequested;
    mx::FilePath _captureFilename;
    bool _exitRequested;
};

#endif // MATERIALX_BUILD_GEN_OSL

#endif