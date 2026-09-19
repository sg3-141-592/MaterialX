//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_OSLRENDERER_H
#define MATERIALX_OSLRENDERER_H

/// @file
/// OSL code renderer

#include <MaterialXRenderOsl/Export.h>

#include <MaterialXRender/ImageHandler.h>
#include <MaterialXRender/ShaderRenderer.h>

MATERIALX_NAMESPACE_BEGIN

// Shared pointer to an OslRenderer
using OslRendererPtr = std::shared_ptr<class OslRenderer>;

/// @class OslRenderer
/// Helper class for rendering generated OSL code to produce images.
///
/// The main services provided are:
///     - Source code validation: Use of "oslc" to compile and test output results
///     - Introspection check: None at this time.
///     - Binding: None at this time.
///     - Render validation: Use of "testrender" to output rendered images. Assumes source compilation was success
///       as it depends on the existence of corresponding .oso files.
///
class MX_RENDEROSL_API OslRenderer : public ShaderRenderer
{
  public:
    /// Create an OSL renderer instance
    static OslRendererPtr create(unsigned int width = 512, unsigned int height = 512, Image::BaseType baseType = Image::BaseType::UINT8);

    /// Destructor
    virtual ~OslRenderer();

    /// Color closure OSL string
    static string OSL_CLOSURE_COLOR_STRING;

    /// @name Setup
    /// @{

    /// Internal initialization required for program validation and rendering.
    /// An exception is thrown on failure.
    /// The exception will contain a list of initialization errors.
    void initialize(RenderContextHandle renderContextHandle = nullptr) override;

    /// @}
    /// @name Rendering
    /// @{

    /// Create OSL program based on an input shader
    ///
    /// A valid executable and include path must be specified before calling this method.
    /// setOslCompilerExecutable(), and setOslIncludePath().
    ///
    /// Additionally setOslOutputFilePath() should be set to allow for output of .osl and .oso
    /// files to the appropriate path location to be used as input for render validation.
    ///
    /// If render validation is not required, then the same temporary name will be used for
    /// all shaders validated using this method.
    /// @param shader Input shader
    void createProgram(ShaderPtr shader) override;

    /// Create OSL program based on shader stage source code.
    /// @param stages Map of name and source code for the shader stages.
    void createProgram(const StageMap& stages) override;

    /// Validate inputs for the compiled OSL program.
    /// Note: Currently no validation has been implemented.
    void validateInputs() override;

    /// Set the size for rendered image
    void setSize(unsigned int width, unsigned int height) override;

    /// Render OSL program to disk.
    /// This is done by using either "testshade" or "testrender".
    /// Currently only "testshade" is supported.
    ///
    /// Usage of both executables requires compiled source (.oso) files as input.
    /// A shader output must be set before running this test via the setOslOutputName() method to
    /// ensure that the appropriate .oso files can be located.
    void render() override;

    /// @}
    /// @name Utilities
    /// @{

    /// Capture the current rendered output as an image.
    ImagePtr captureImage(ImagePtr image = nullptr) override;

    /// @}
    /// @name Compilation settings
    /// @{

    /// Set the path to the OSL executable. Note that it is assumed that this
    /// references the location of the oslc executable.
    /// @param executableFilePath Path to OSL compiler executable
    void setOslCompilerExecutable(const FilePath& executableFilePath)
    {
        _oslCompilerExecutable = executableFilePath;
    }

    /// Set the search locations for OSL include files.
    /// @param dirPath Include path(s) for the OSL compiler. This should include the
    /// path to stdosl.h.
    void setOslIncludePath(const FileSearchPath& dirPath)
    {
        _oslIncludePath = dirPath;
    }

    /// Set the location where compiled OSL files will reside.
    /// @param dirPath Path to output location
    void setOslOutputFilePath(const FilePath& dirPath)
    {
        _oslOutputFilePath = dirPath;
    }

    /// Set shader parameter strings to be added to the scene XML file. These
    /// strings will set parameter overrides for the shader.
    void setShaderParameterOverrides(const StringVec& parameterOverrides)
    {
        _oslShaderParameterOverrides = parameterOverrides;
    }

    /// Set shader parameter strings to be added to the scene XML file. These
    /// strings will set parameter overrides for the shader.
    void setEnvShaderParameterOverrides(const StringVec& parameterOverrides)
    {
        _envOslShaderParameterOverrides = parameterOverrides;
    }

    /// Set the OSL shader output.
    /// This is used during render validation if "testshade" or "testrender" is executed.
    /// For testrender this value is used to replace the %shader_output% token in the
    /// input scene file.
    /// @param outputName Name of shader output
    /// @param outputType The MaterialX type of the output
    void setOslShaderOutput(const string& outputName, const string& outputType)
    {
        _oslShaderOutputName = outputName;
        _oslShaderOutputType = outputType;
    }

    /// Set the path to the OSL shading tester. Note that it is assumed that this
    /// references the location of the "testshade" executable.
    /// @param executableFilePath Path to OSL "testshade" executable
    void setOslTestShadeExecutable(const FilePath& executableFilePath)
    {
        _oslTestShadeExecutable = executableFilePath;
    }

    /// Set the path to the OSL rendering tester. Note that it is assumed that this
    /// references the location of the "testrender" executable.
    /// @param executableFilePath Path to OSL "testrender" executable
    void setOslTestRenderExecutable(const FilePath& executableFilePath)
    {
        _oslTestRenderExecutable = executableFilePath;
    }

    /// Set the XML scene file to use for testrender. This is a template file
    /// with the following tokens for replacement:
    ///     - %shader% : which will be replaced with the name of the shader to use
    ///     - %shader_output% : which will be replace with the name of the shader output to use
    /// @param templateFilePath Scene file name
    void setOslTestRenderSceneTemplateFile(const FilePath& templateFilePath)
    {
        _oslTestRenderSceneTemplateFile = templateFilePath;
    }

    /// Set the name of the shader to be used for the input XML scene file.
    /// The value is used to replace the %shader% token in the file.
    /// @param shaderName Name of shader
    void setOslShaderName(const string& shaderName)
    {
        _oslShaderName = shaderName;
    }

    /// Set the search path for dependent shaders (.oso files) which are used
    /// when rendering with testrender.
    /// @param dirPath Path to location containing .oso files.
    void setOslUtilityOSOPath(const FilePath& dirPath)
    {
        _oslUtilityOSOPath = dirPath;
    }

    /// Set additional search paths for dependent shaders (.oso files) which are
    /// used when rendering with testrender, e.g. the OSL distribution's shaders
    /// directory containing built-in shaders such as "matte" and "emitter".
    /// @param dirPaths Search paths to locations containing .oso files.
    void setOslShaderSearchPath(const FileSearchPath& dirPaths)
    {
        _oslShaderSearchPath = dirPaths;
    }
    void setDataLibraryOSOPath(const FilePath& dirPath)
    {
        _dataLibraryOSOPath = dirPath;
    }

    /// Used to toggle to either use testrender or testshade during render validation
    /// By default testshade is used.
    /// @param useTestRender Indicate whether to use testrender.
    void useTestRender(bool useTestRender)
    {
        _useTestRender = useTestRender;
    }

    /// Used to switch between testing oso files and osl command strings
    void useOslCommandString(bool useOslCmdstr)
    {
        _useOSLCmdStr = useOslCmdstr;
    }

    /// Set the testrender `-aa N` value used for lit (closure-color) outputs.
    /// Note: testrender treats this as the linear AA dimension and traces
    /// `N * N` samples per pixel, so cost scales quadratically.
    void setAaLit(int aa)
    {
        _aaLit = aa;
    }

    /// Set the testrender `-aa N` value used for unlit (non-closure) outputs.
    /// Same `N * N` semantics as setAaLit.
    void setAaUnlit(int aa)
    {
        _aaUnlit = aa;
    }

    /// Set the testrender `-t N` value, the number of render threads.
    /// @param threads Number of threads to use.
    void setThreads(int threads)
    {
        _threads = threads;
    }

    /// Set the camera to use for the testrender scene. The values are used to
    /// replace the %camera_eye%, %camera_look_at%, %camera_up% and
    /// %camera_fov% tokens in the scene template.
    /// @param eye Camera position.
    /// @param lookAt Camera target point.
    /// @param up Camera up vector.
    /// @param fov Vertical field of view, in degrees.
    void setCamera(const Vector3& eye, const Vector3& lookAt, const Vector3& up, float fov)
    {
        _cameraEye = eye;
        _cameraLookAt = lookAt;
        _cameraUp = up;
        _cameraFov = fov;
    }

    /// Set the osl command string that is to be tested
    void setOSLCmdStr(const string& oslCmd)
    {
        _oslCmdStr = oslCmd;
    }
    ///
    /// Compile OSL code stored in a file. Will throw an exception if an error occurs.
    /// @param oslFilePath OSL file path.
    void compileOSL(const FilePath& oslFilePath);

    /// @}

  protected:
    ///
    /// Shade using OSO input file. Will throw an exception if an error occurs.
    /// @param dirPath Path to location containing input .oso file.
    /// @param shaderName Name of OSL shader. A corresponding .oso file is assumed to exist in the output path folder.
    /// @param outputName Name of OSL shader output to use.
    void shadeOSL(const FilePath& dirPath, const string& shaderName, const string& outputName);

    ///
    /// Render using OSO input file. Will throw an exception if an error occurs.
    /// @param dirPath Path to location containing input .oso file.
    /// @param shaderName Name of OSL shader. A corresponding .oso file is assumed to exist in the output path folder.
    /// @param outputName Name of OSL shader output to use.
    void renderOSL(const FilePath& dirPath, const string& shaderName, const string& outputName);

    /// Render using OSL command string. Will throw an exception if an error occurs.
    /// @param dirPath Path to location containing input .oso file.
    /// @param shaderName Name of OSL shader. A corresponding .oso file is assumed to exist in the output path folder.
    void renderOSLNetwork(const FilePath& dirPath, const string& shaderName);

    /// Constructor
    OslRenderer(unsigned int width, unsigned int height, Image::BaseType baseType);

  private:
    FilePath _oslCompilerExecutable;
    FileSearchPath _oslIncludePath;
    FilePath _oslOutputFilePath;
    FilePath _oslOutputFileName;

    FilePath _oslTestShadeExecutable;
    FilePath _oslTestRenderExecutable;
    FilePath _oslTestRenderSceneTemplateFile;
    string _oslShaderName;
    StringVec _oslShaderParameterOverrides;
    StringVec _envOslShaderParameterOverrides;
    string _oslShaderOutputName;
    string _oslShaderOutputType;
    FilePath _oslUtilityOSOPath;
    FileSearchPath _oslShaderSearchPath;
    FilePath _dataLibraryOSOPath;
    bool _useTestRender;
    bool _useOSLCmdStr;
    int _aaLit;
    int _aaUnlit;
    int _threads;
    string _oslCmdStr;

    // Camera used for the testrender scene template.
    Vector3 _cameraEye = Vector3(0.0f, 1.4f, 6.0f);
    Vector3 _cameraLookAt = Vector3(0.0f, 1.0f, 0.0f);
    Vector3 _cameraUp = Vector3(0.0f, 1.0f, 0.0f);
    float _cameraFov = 30.0f;
};

MATERIALX_NAMESPACE_END

#endif
