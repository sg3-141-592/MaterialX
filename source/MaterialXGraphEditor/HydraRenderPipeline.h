//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_HYDRARENDERPIPELINE_H
#define MATERIALX_HYDRARENDERPIPELINE_H

#ifdef MATERIALX_BUILD_RENDER_HYDRA

#include <MaterialXGraphEditor/RenderPipeline.h>

#include <memory>
#include <string>

class HydraRenderPipeline;
using HydraRenderPipelinePtr = std::shared_ptr<HydraRenderPipeline>;

/// @class HydraRenderPipeline
/// Hydra render backend for the graph editor.
///
/// The pipeline converts the current MaterialX scene to a transient USD stage
/// and renders it through a Hydra render delegate.  The delegate is selected
/// by name (for example "Cycles" or "GL"), so the same pipeline can host
/// additional renderers without a new Graph Editor view class.
class HydraRenderPipeline : public RenderPipeline
{
  public:
    HydraRenderPipeline(mx::DocumentPtr doc,
                        mx::DocumentPtr stdLib,
                        const std::string& meshFilename,
                        const std::string& envRadianceFilename,
                        const mx::FileSearchPath& searchPath,
                        int viewWidth,
                        int viewHeight,
                        const std::string& delegateId,
                        const std::string& renderPass = "combined");
    ~HydraRenderPipeline() override;

    /// Configure USD/Hydra plugin discovery before the first USD call.
    static void configureEnvironment();

    /// Return true if the named Hydra delegate is available in this build.
    static bool isDelegateAvailable(const std::string& delegateName);

    std::string getName() const override
    {
        return _displayName;
    }

    RenderCapabilities getCapabilities() const override;
    void initialize() override;
    void setDocument(mx::DocumentPtr document) override;
    void updateMaterials(mx::TypedElementPtr typedElem) override;
    void loadMesh(const mx::FilePath& filename) override;
    void setSettings(const RenderSettings& settings) override;
    void resize(int width, int height) override;
    void setCamera(mx::CameraPtr camera, const RenderCameraState& state) override;
    void render() override;
    unsigned int getColorTexture() const override;
    void requestFrameCapture(const mx::FilePath& filename) override;
    bool isFrameCaptureComplete() const override;
    bool isNodeDefSupported(const mx::NodeDefPtr& nodeDef) override;
    void modifyUniform(const std::string& name, mx::ValuePtr value) override;
    void setActive(bool active) override;
    mx::ImageHandlerPtr getImageHandler() const override;
    const mx::StringSet& getXincludeFiles() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    std::string _displayName;
};

#endif // MATERIALX_BUILD_RENDER_HYDRA

#endif
