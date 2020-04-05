#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;
    ::dfs_service::file_response server_response;
    dfs_service::file_stream client_request;
    std::string file_name = WrapPath(filename);

    std::ifstream file_reader;
    file_reader.open(file_name, std::ios::in | std::ios::binary);
    if (!file_reader.is_open()) {
        dfs_log(LL_ERROR) << "File Not exist: " << file_name << strerror(errno);
        return StatusCode::NOT_FOUND;
    } else {
        std::unique_ptr<ClientWriter<::dfs_service::file_stream>> request_writer(
                service_stub->store_file(&context, &server_response));

        client_request.set_file_name(filename);
        request_writer->Write(client_request);
        char buffer[BUFFER_SIZE];

        while (file_reader.read(buffer, BUFFER_SIZE - 1)) {
//            dfs_log(LL_DEBUG2) << "BUFFER :  " << buffer;
            client_request.set_file_data(buffer, BUFFER_SIZE - 1);
            bzero(buffer, BUFFER_SIZE);
            request_writer->Write(client_request);
            client_request.clear_file_data();
        }

// Flush Last Bites
        if (file_reader.gcount() > 0) {
//            dfs_log(LL_DEBUG2) << "EOF BUFFER :  " << file_reader.gcount()  ;
//            dfs_log(LL_DEBUG2) << buffer  ;
            client_request.set_file_data(buffer, file_reader.gcount());
            request_writer
                    ->Write(client_request);
            client_request.clear_file_data();
        }

        client_request.clear_file_data();
        file_reader.close();


        request_writer->WritesDone();
        method_status = request_writer->Finish();
//        dfs_log(LL_DEBUG) << "Method Status : " + method_status.error_details();
        if (method_status.ok()) {
            if (server_response.file_transfer_status() == FILE_TRANSFER_SUCCESS) {
                return StatusCode::OK;
            } else {
                return StatusCode::NOT_FOUND;
            }
        } else {
            dfs_log(LL_ERROR) << method_status.error_details();
            dfs_log(LL_ERROR) << "Error Message :" << method_status.error_message();
            return method_status.error_code();
        }

    }


}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //


    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;
    ::dfs_service::file_stream server_response;
    dfs_service::file_request client_request;
    std::string file_name = WrapPath(filename);
    std::ofstream file_writer;
    client_request.set_file_name(filename);
    std::unique_ptr<ClientReader<::dfs_service::file_stream>> request_reader(
            service_stub->fetch_file(&context, client_request));

    file_writer.open(file_name, std::ios::out | std::ios::binary);
    request_reader->Read(&server_response);
    dfs_log(LL_DEBUG3) << "Response File name" << server_response.file_name().c_str();
    auto *file_stats = (struct stat *) server_response.file_stats().c_str();
    struct timespec modify_time = file_stats->st_mtim;

//    char buffer[BUFFER_SIZE];
    if (file_writer.is_open()) {
        while (request_reader->Read(&server_response)) {
            dfs_log(LL_DEBUG3) << "Server Buffer" << server_response.file_data().c_str();
//            file_writer << server_response.file_data().c_str();
            file_writer.write(server_response.file_data().c_str(), server_response.file_data().size());
        }
        if (file_writer.bad() || file_writer.fail()) {
            dfs_log(LL_ERROR) << "File Opening Failed" << file_name;
            return StatusCode::CANCELLED;
        }
        if (file_writer.good()) {
            file_writer.close();
        }
    } else {
        dfs_log(LL_ERROR) << "File Opening Failed" << file_name;
        return StatusCode::CANCELLED;
    }
    dfs_log(LL_SYSINFO) << "File name =" << file_name << "mtime = " << modify_time.tv_sec << "filesize = "
                        << file_stats->st_size;

    method_status = request_reader->Finish();
//    dfs_log(LL_DEBUG) << "Method Status : " << method_status;
    if (method_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << method_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << method_status.error_message();
        return method_status.error_code();
    }


}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;

    ::dfs_service::file_response server_response;
    dfs_service::file_request client_request;

    client_request.set_file_name(filename);
    Status server_status = service_stub->delete_file(&context, client_request, &server_response);


//    dfs_log(LL_DEBUG) << "Method Status : " << server_status;
    if (server_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << server_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << server_status.error_message();
        return server_status.error_code();
    }

}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;

    ::dfs_service::file_list server_response;
    dfs_service::empty client_request;
    struct file_object {
        char file_path[256];
        std::int32_t mtime;
    } *temp_file_object;
//    temp_file_object = (struct file_object *) malloc(sizeof(struct file_object));
//    if (temp_file_object == NULL) {
//        dfs_log(LL_SYSINFO) << "Malloc Failed....";
//    }
    std::unique_ptr<ClientReader<::dfs_service::file_list>> request_reader(
            service_stub->list_file(&context, client_request));

    while (request_reader->Read(&server_response)) {
        temp_file_object = (file_object *) server_response.files().c_str();
        dfs_log(LL_DEBUG2) << std::string(temp_file_object->file_path);

        file_map->insert(
                std::pair<std::string, int>(std::string(temp_file_object->file_path), temp_file_object->mtime));
    }
//    free(temp_file_object);
    method_status = request_reader->Finish();

//    dfs_log(LL_DEBUG) << "Method Status : " << method_status;
    if (method_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << method_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << method_status.error_message();
        return method_status.error_code();
    }
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;

    ::dfs_service::file_response server_response;
    dfs_service::file_request client_request;

    client_request.set_file_name(filename);
    Status server_status = service_stub->stat_file(&context, client_request, &server_response);

    if (server_response.file_transfer_status() == FILE_TRANSFER_FAILURE) {
        return StatusCode::NOT_FOUND;
    }

    file_status = (struct stat *) server_response.file_stats().c_str();
    dfs_log(LL_DEBUG) << ((struct stat *) server_response.file_stats().c_str())->st_size;
    if (server_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << server_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << server_status.error_message();
        return server_status.error_code();
    }


}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//

