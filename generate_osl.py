import MaterialX as mx
import MaterialX.PyMaterialXGenOsl as mx_gen_osl

shadergen = mx_gen_osl.OslShaderGenerator.create()
context = mx_gen_shader.GenContext(shadergen)
context.registerSourceCodeSearchPath(codeSearchPath)
shadergen.registerTypeDefs(doc);