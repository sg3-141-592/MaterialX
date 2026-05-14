//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXTest/External/Catch/catch.hpp>

#ifdef CATCH_CONFIG_ENABLE_BENCHMARKING

    #include <MaterialXCore/Definition.h>
    #include <MaterialXCore/Document.h>
    #include <MaterialXCore/Node.h>

namespace mx = MaterialX;

// Build a synthetic document that exercises NodeGraph::getNodeDef from
// PortElement::validate.  Two amplification axes, mirroring the original
// research's amplify.py:
//   * numImplStubs    -> document-root children that the legacy linear
//                        getImplementations() scan must walk
//   * numNodegraphRefs -> nodegraph-connected inputs, each one triggering
//                         a fresh scan
// Pre-fix, doc->validate() is O(numImplStubs * numNodegraphRefs).
static mx::DocumentPtr buildAmplifiedDocument(size_t numImplStubs, size_t numNodegraphRefs)
{
    mx::DocumentPtr doc = mx::createDocument();

    // A nodegraph with one internal node and one output, so that
    // PortElement::getConnectedNode() resolves transitively through the
    // output (see Input::getConnectedNode in Interface.cpp).
    mx::NodeGraphPtr ng = doc->addNodeGraph("ng");
    mx::NodePtr inner = ng->addNode("constant", "inner", "color3");
    mx::OutputPtr out = ng->addOutput("out", "color3");
    out->setNodeName(inner->getName());

    // A consumer node holding many inputs that reference the nodegraph.
    mx::NodePtr consumer = doc->addNode("surface", "consumer", "surfaceshader");
    for (size_t i = 0; i < numNodegraphRefs; ++i)
    {
        mx::InputPtr in = consumer->addInput("in" + std::to_string(i), "color3");
        in->setNodeGraphString("ng");
        in->setOutputString("out");
    }

    // Document-root <implementation> stubs (Mutation A in amplify.py).
    for (size_t i = 0; i < numImplStubs; ++i)
    {
        mx::ImplementationPtr impl = doc->addImplementation("i" + std::to_string(i));
        impl->setNodeDefString("n" + std::to_string(i));
    }

    return doc;
}

TEST_CASE("NodeGraph getNodeDef performance", "[node][performance]")
{
    BENCHMARK("validate, stubs=200 refs=100")
    {
        mx::DocumentPtr doc = buildAmplifiedDocument(200, 100);
        return doc->validate();
    };

    BENCHMARK("validate, stubs=1000 refs=100")
    {
        mx::DocumentPtr doc = buildAmplifiedDocument(1000, 100);
        return doc->validate();
    };

    BENCHMARK("validate, stubs=10000 refs=100")
    {
        mx::DocumentPtr doc = buildAmplifiedDocument(10000, 100);
        return doc->validate();
    };
}

#endif // CATCH_CONFIG_ENABLE_BENCHMARKING
