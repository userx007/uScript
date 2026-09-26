#include "grpc_protocol.hpp"

#include <fstream>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/util/json_util.h>
#include <sstream>

bool GrpcProtocol::loadDescriptorSet(const std::string &strProtosetPath, std::string &strOutError)
{
    m_bLoaded = false;

    std::ifstream in(strProtosetPath, std::ios::binary);
    if (!in) {
        strOutError = "cannot open descriptor set file: " + strProtosetPath;
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    google::protobuf::FileDescriptorSet fdSet;
    if (!fdSet.ParseFromString(ss.str())) {
        strOutError = "not a valid FileDescriptorSet (produce it with: protoc --descriptor_set_out=... "
                   "--include_imports your.proto): " +
                   strProtosetPath;
        return false;
    }

    for (const auto &fileProto : fdSet.file()) {
        // Already-built files (a shared import pulled in by two top-level
        // .proto files in the same set) are not an error — BuildFile()
        // returns nullptr for them too, so only fail if the pool doesn't
        // already know this exact file under this exact name.
        if (m_pool.FindFileByName(fileProto.name()) != nullptr) {
            continue;
        }
        if (m_pool.BuildFile(fileProto) == nullptr) {
            strOutError = "failed to build descriptor for '" + fileProto.name() +
                       "' (malformed or missing an import not included in the set)";
            return false;
        }
    }

    m_bLoaded = true;
    return true;
}

const google::protobuf::MethodDescriptor *GrpcProtocol::resolveMethod(const std::string &strMethodPath,
                                                                      std::string &strOutError) const
{
    if (!m_bLoaded) {
        strOutError = "no descriptor set loaded — set GRPC.CONFIG d=<path.protoset> first";
        return nullptr;
    }

    // Accept both "package.Service/Method" (the gRPC wire path convention,
    // minus the leading '/') and "package.Service.Method" (all-dotted, in
    // case a script author copies it straight out of a .proto file).
    std::string serviceName;
    std::string methodName;
    auto slashPos = strMethodPath.find('/');
    if (slashPos != std::string::npos) {
        serviceName = strMethodPath.substr(0, slashPos);
        methodName  = strMethodPath.substr(slashPos + 1);
    } else {
        auto dotPos = strMethodPath.rfind('.');
        if (dotPos == std::string::npos) {
            strOutError = "malformed method path '" + strMethodPath + "' — expected package.Service/Method";
            return nullptr;
        }
        serviceName = strMethodPath.substr(0, dotPos);
        methodName  = strMethodPath.substr(dotPos + 1);
    }

    const auto *serviceDesc = m_pool.FindServiceByName(serviceName);
    if (!serviceDesc) {
        strOutError = "unknown service '" + serviceName + "' (not present in the loaded descriptor set)";
        return nullptr;
    }
    const auto *methodDesc = serviceDesc->FindMethodByName(methodName);
    if (!methodDesc) {
        strOutError = "unknown method '" + methodName + "' on service '" + serviceName + "'";
        return nullptr;
    }
    // Every RPC shape is accepted here — grpc_driver.hpp's send() dispatches
    // on method->client_streaming()/server_streaming() to pick unary,
    // server-streaming, client-streaming, or bidi handling.
    return methodDesc;
}

std::unique_ptr<google::protobuf::Message> GrpcProtocol::newRequestMessage(
    const google::protobuf::MethodDescriptor *pMethod) const
{
    const google::protobuf::Message *prototype = m_factory.GetPrototype(pMethod->input_type());
    return std::unique_ptr<google::protobuf::Message>(prototype->New());
}

std::unique_ptr<google::protobuf::Message> GrpcProtocol::newResponseMessage(
    const google::protobuf::MethodDescriptor *pMethod) const
{
    const google::protobuf::Message *prototype = m_factory.GetPrototype(pMethod->output_type());
    return std::unique_ptr<google::protobuf::Message>(prototype->New());
}

bool GrpcProtocol::parseJsonIntoMessage(const std::string &strJsonText, google::protobuf::Message &message,
                                        std::string &strOutError) const
{
    message.Clear();
    const std::string &toParse = strJsonText.empty() ? std::string("{}") : strJsonText;

    auto status                = google::protobuf::util::JsonStringToMessage(toParse, &message);
    if (!status.ok()) {
        strOutError = status.ToString();
        return false;
    }
    return true;
}

bool GrpcProtocol::messageToJson(const google::protobuf::Message &message, std::string &strOutText) const
{
    auto status = google::protobuf::util::MessageToJsonString(message, &strOutText);
    if (!status.ok()) {
        strOutText = status.ToString();
        return false;
    }
    return true;
}
