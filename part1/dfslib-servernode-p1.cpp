#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::DFSService;


//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    int write_to_file(std::string filepath, ::grpc::ServerReader<::dfs_service::file_stream> *reader) {
        std::ofstream file_writer;
        file_writer.open(filepath, std::ios::out | std::ios::binary);
        ::dfs_service::file_stream temp;
        if (file_writer.is_open()) {
            while (reader->Read(&temp)) {
                file_writer << temp.file_data();
            }
            if (file_writer.bad() || file_writer.fail()) {
                dfs_log(LL_ERROR) << "File Opening Failed" << filepath;
                return -1;
            }
            if (file_writer.good()) {
                file_writer.close();
                return 0;
            }
        } else {
            dfs_log(LL_ERROR) << "File Opening Failed" << filepath;
            return -1;
        }
    }

    int read_file(std::string filepath, ::grpc::ServerWriter<::dfs_service::file_stream> *writer) {
        std::ifstream file_reader;
        file_reader.open(filepath, std::ios::in | std::ios::binary);
        ::dfs_service::file_stream temp;
        char buffer[BUFSIZ];
        if (file_reader.is_open()) {
            while (!file_reader.eof()) {
                file_reader.read(buffer, BUFSIZ - 1);
                temp.set_file_data(buffer);
                writer->Write(temp);
                temp.clear_file_data();
            }
            temp.clear_file_data();
            if (file_reader.bad() || file_reader.fail()) {
                dfs_log(LL_ERROR) << "File Reading Failed" << filepath << strerror(errno);
                return -1;
            }
            if (file_reader.good()) {
                file_reader.close();
                return 0;
            }
        } else {
            dfs_log(LL_ERROR) << "File Opening Failed" << filepath;
            return -1;
        }
    }

    struct stat get_file_stats(std::string filepath) {
        struct stat file_stat;
        if (stat(filepath.c_str(), &file_stat) == -1) {
            dfs_log(LL_ERROR) << "File Stats Failed for file." << filepath << strerror(errno);
        }
        return file_stat;
    }


public:

    DFSServiceImpl(const std::string &mount_path) : mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //
    ::grpc::Status store_file(::grpc::ServerContext *context, ::grpc::ServerReader<::dfs_service::file_stream> *reader,
                              ::dfs_service::file_response *response) override {

        dfs_log(LL_DEBUG3) << "Start Storing The File.";
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }
//    Define Variable
        ::dfs_service::file_stream file_content;

        reader->Read(&file_content);
        std::string file_name = WrapPath(file_content.file_name());
        if (write_to_file(file_name, reader) == -1) {
            dfs_log(LL_ERROR) << "Write To file Failed.";
            response->set_file_transfer_status(FILE_TRANSFER_FAILURE);
            return ::grpc::Status(::grpc::StatusCode::INTERNAL, "Write of File Failed.");
        }

        dfs_log(LL_DEBUG3) << "Successfully Written the File and Getting Stats";
        response->set_file_transfer_status(FILE_TRANSFER_SUCCESS);

        struct stat file_status = get_file_stats(file_name);
        response->set_file_name(file_name);
        response->set_file_stats(&file_status, sizeof(file_status));

        dfs_log(LL_DEBUG3) << "Successfully Sent the resposne and sending reply";

        return ::grpc::Status::OK;

    }

    ::grpc::Status fetch_file(::grpc::ServerContext *context, const ::dfs_service::file_request *request,
                              ::grpc::ServerWriter<::dfs_service::file_stream> *writer) override {
        dfs_log(LL_DEBUG3) << "Start Storing The File.";
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }
        std::string file_name = WrapPath(request->file_name());
        ::dfs_service::file_stream file_content;
        file_content.set_file_name(file_name);

        if (read_file(file_name, writer) == -1) {
            dfs_log(LL_ERROR) << "Write To file Failed.";
            return ::grpc::Status(::grpc::StatusCode::INTERNAL, "Read of File Failed.");
        }

        struct stat temp_file_stats = get_file_stats(file_name);
        file_content.set_file_stats(&temp_file_stats, sizeof(temp_file_stats));
        writer->Write(file_content);
        return ::grpc::Status::OK;
    }
};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}





//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//

