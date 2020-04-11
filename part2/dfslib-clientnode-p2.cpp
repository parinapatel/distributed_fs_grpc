#include <regex>
#include <mutex>
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
#include <utime.h>
#include <dirent.h>

#include "src/dfs-utils.h"
#include "src/dfslibx-clientnode-p2.h"
#include "dfslib-shared-p2.h"
#include "dfslib-clientnode-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;


extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using to indicate
// a file request and a listing of files from the server.
//
using FileRequestType = dfs_service::empty;
using FileListResponseType = dfs_service::file_list;

DFSClientNodeP2::DFSClientNodeP2() : DFSClientNode() {}

DFSClientNodeP2::~DFSClientNodeP2() {}

grpc::StatusCode DFSClientNodeP2::RequestWriteAccess(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
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
    client_request.set_client_id(DFSClientNode::ClientId());
    Status server_status = service_stub->lock_file(&context, client_request, &server_response);

    if (server_status.ok()) {
        if (server_response.file_lock() == LOCKED) {
            return StatusCode::OK;
        } else return StatusCode::RESOURCE_EXHAUSTED;
    } else {
        dfs_log(LL_ERROR) << server_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << server_status.error_message();
        return server_status.error_code();
    }
}

grpc::StatusCode DFSClientNodeP2::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // stored is the same on the server (i.e. the ALREADY_EXISTS gRPC response).
    //
    // You will also need to add a request for a write lock before attempting to store.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
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
    std::string file_path = WrapPath(filename);

    std::uint32_t client_file_crc = dfs_file_checksum(file_path, &this->crc_table);

    client_request.set_file_crc(client_file_crc);
    client_request.set_client_id(DFSClientNode::ClientId());
    std::ifstream file_reader;
    file_reader.open(file_path, std::ios::in | std::ios::binary);

    if (!file_reader.is_open()) {
        dfs_log(LL_ERROR) << "File Not exist: " << file_path << strerror(errno);
        return StatusCode::NOT_FOUND;
    } else {
        std::unique_ptr<ClientWriter<::dfs_service::file_stream>> request_writer(
                service_stub->store_file(&context, &server_response));

        client_request.set_file_name(filename);
        request_writer->Write(client_request);
        char buffer[BUFFER_SIZE];

        while (file_reader.read(buffer, BUFFER_SIZE - 1)) {
            client_request.set_file_data(buffer, BUFFER_SIZE - 1);
            bzero(buffer, BUFFER_SIZE);
            request_writer->Write(client_request);
            client_request.clear_file_data();
        }

// Flush Last Bites
        if (file_reader.gcount() > 0) {
            client_request.set_file_data(buffer, file_reader.gcount());
            request_writer
                    ->Write(client_request);
            client_request.clear_file_data();
        }

        client_request.clear_file_data();
        file_reader.close();


        request_writer->WritesDone();
        method_status = request_writer->Finish();
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


grpc::StatusCode DFSClientNodeP2::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // fetched is the same on the client (i.e. the files do not differ
    // between the client and server and a fetch would be unnecessary.
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // DEADLINE_EXCEEDED - if the deadline timeout occurs
    // NOT_FOUND - if the file cannot be found on the server
    // ALREADY_EXISTS - if the local cached file has not changed from the server version
    // CANCELLED otherwise
    //
    // Hint: You may want to match the mtime on local files to the server's mtime
    //
    ClientContext context;
    std::chrono::system_clock::time_point timeout =
            std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(timeout);
    ::grpc::Status method_status;
    ::dfs_service::file_stream server_response;
    dfs_service::file_request client_request;
    std::string file_path = WrapPath(filename);
    std::ofstream file_writer;

    client_request.set_file_name(filename);
    client_request.set_client_id(DFSClientNode::ClientId());

    std::unique_ptr<ClientReader<::dfs_service::file_stream>> request_reader(
            service_stub->fetch_file(&context, client_request));

    request_reader->Read(&server_response);
    if (check_file_content(file_path, &this->crc_table, server_response.file_crc()) == FILE_EXIST)
        return ::grpc::StatusCode::ALREADY_EXISTS;


    file_writer.open(file_path, std::ios::out | std::ios::binary);

    if (file_writer.is_open()) {
        while (request_reader->Read(&server_response)) {
//            file_writer << server_response.file_data().c_str();
            file_writer.write(server_response.file_data().c_str(), server_response.file_data().size());
        }
        if (file_writer.bad() || file_writer.fail()) {
            dfs_log(LL_ERROR) << "File Opening Failed" << file_path;
            return StatusCode::CANCELLED;
        }
        if (file_writer.good()) {
            file_writer.close();
        }
    } else {
        dfs_log(LL_ERROR) << "File Opening Failed" << file_path;
        return StatusCode::CANCELLED;
    }
    method_status = request_reader->Finish();
    if (method_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << method_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << method_status.error_message();
        return method_status.error_code();
    }

}

grpc::StatusCode DFSClientNodeP2::Delete(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
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
    client_request.set_client_id(DFSClientNode::ClientId());

    Status server_status = service_stub->delete_file(&context, client_request, &server_response);


    if (server_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << server_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << server_status.error_message();
        return server_status.error_code();
    }

}

grpc::StatusCode DFSClientNodeP2::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list files here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // listing details that would be useful to your solution to the list response.
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

    struct file_object *temp_file_object;

    std::unique_ptr<ClientReader<::dfs_service::file_list>> request_reader(
            service_stub->list_file(&context, client_request));

    if (display) {
        dfs_log(LL_SYSINFO) << "File Path \t Modified Time";
    }

    while (request_reader->Read(&server_response)) {
        temp_file_object = (file_object *) server_response.files().c_str();
        dfs_log(LL_DEBUG2) << " List: " << std::string(temp_file_object->file_path);
        if (display) {
            dfs_log(LL_SYSINFO) << temp_file_object->file_path << "\t" << temp_file_object->mtime;
        }
        file_map->insert(
                std::pair<std::string, int>(std::string(temp_file_object->file_path), temp_file_object->mtime));
    }
    method_status = request_reader->Finish();

    if (method_status.ok()) {
        return StatusCode::OK;
    } else {
        dfs_log(LL_ERROR) << method_status.error_details();
        dfs_log(LL_ERROR) << "Error Message :" << method_status.error_message();
        return method_status.error_code();
    }

}

grpc::StatusCode DFSClientNodeP2::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // status details that would be useful to your solution.
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
    client_request.set_client_id(DFSClientNode::ClientId());

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

void DFSClientNodeP2::InotifyWatcherCallback(std::function<void()> callback) {

    //
    // STUDENT INSTRUCTION:
    //
    // This method gets called each time inotify signals a change
    // to a file on the file system. That is every time a file is
    // modified or created.
    //
    // You may want to consider how this section will affect
    // concurrent actions between the inotify watcher and the
    // asynchronous callbacks associated with the server.
    //
    // The callback method shown must be called here, but you may surround it with
    // whatever structures you feel are necessary to ensure proper coordination
    // between the async and watcher threads.
    //
    // Hint: how can you prevent race conditions between this thread and
    // the async thread when a file event has been signaled?
    //

    std::lock_guard<std::mutex> lock(inotify_mutex);
    callback();

}

//
// STUDENT INSTRUCTION:
//
// This method handles the gRPC asynchronous callbacks from the server.
// We've provided the base structure for you, but you should review
// the hints provided in the STUDENT INSTRUCTION sections below
// in order to complete this method.
//
void DFSClientNodeP2::HandleCallbackList() {

    void* tag;

    bool ok = false;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your file list synchronization code here.
    //
    // When the server responds to an asynchronous request for the CallbackList,
    // this method is called. You should then synchronize the
    // files between the server and the client based on the goals
    // described in the readme.
    //
    // In addition to synchronizing the files, you'll also need to ensure
    // that the async thread and the file watcher thread are cooperating. These
    // two threads could easily get into a race condition where both are trying
    // to write or fetch over top of each other. So, you'll need to determine
    // what type of locking/guarding is necessary to ensure the threads are
    // properly coordinated.
    //

    DIR *current_dir;
    struct dirent *entry;
    struct stat file_stat{};

    struct file_object temp_file_object{};

    // Block until the next result is available in the completion queue.
    while (completion_queue.Next(&tag, &ok)) {
        {
            //
            // STUDENT INSTRUCTION:
            //
            // Consider adding a critical section or RAII style lock here
            //
            std::lock_guard<std::mutex> lock(inotify_mutex);
            // The tag is the memory location of the call_data object
            AsyncClientData<FileListResponseType> *call_data = static_cast<AsyncClientData<FileListResponseType> *>(tag);
            int dir_length = call_data->reply.file_length();
            struct file_object *file_storage_list;

            std::map<std::string, struct file_object> local_storage;
            file_storage_list = (file_object *) call_data->reply.files().c_str();

            dfs_log(LL_DEBUG2) << "Received completion queue callback";

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            // GPR_ASSERT(ok);
            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {

                dfs_log(LL_DEBUG3) << "Handling async callback ";
                current_dir = opendir(mount_path.c_str());
                while ((entry = readdir(current_dir)) != nullptr) {
                    if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0 &&
                        entry->d_name[0] != '.') {
                        stat(WrapPath(entry->d_name).c_str(), &file_stat);
                        strcpy(temp_file_object.file_path, entry->d_name);
                        temp_file_object.mtime = file_stat.st_mtim.tv_sec;
                        temp_file_object.create_time = file_stat.st_ctim.tv_sec;
                        temp_file_object.file_size = file_stat.st_size;
                        dfs_log(LL_DEBUG3) << temp_file_object.file_path;
                        local_storage[temp_file_object.file_path] = temp_file_object;

                    }
                }
                std::string remote_file_path;
                for (int i = 0; i < dir_length; i++) {
                    dfs_log(LL_DEBUG) << "file Name: " << file_storage_list[i].file_path;
                    remote_file_path = file_storage_list[i].file_path;
                    if (local_storage.count(remote_file_path) > 0) {
                        if (file_storage_list[i].file_crc !=
                            dfs_file_checksum(local_storage[remote_file_path].file_path, &this->crc_table)) {
                            if (file_storage_list[i].mtime >= local_storage[remote_file_path].mtime) {
                                this->Fetch(remote_file_path);
                            } else this->Store(remote_file_path);
                        }
                        local_storage.erase(remote_file_path);
                    } else {
                        this->Fetch(remote_file_path);
                    }
                }

                if (!local_storage.empty()) {
                    auto it = local_storage.begin();
                    while (it != local_storage.end()) {
                        if (remove(it->second.file_path) == -1) {
                            dfs_log(LL_ERROR) << it->second.file_path << "Deletion Failed";
                        }
                        it++;
                    }
                }


            } else {
                dfs_log(LL_ERROR) << "Status was not ok. Will try again in " << DFS_RESET_TIMEOUT << " milliseconds.";
                dfs_log(LL_ERROR) << call_data->status.error_message();
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            // Once we're complete, deallocate the call_data object.
            delete call_data;

            //
            // STUDENT INSTRUCTION:
            //
            // Add any additional syncing/locking mechanisms you may need here

        }


        // Start the process over and wait for the next callback response
        dfs_log(LL_DEBUG3) << "Calling InitCallbackList";
        InitCallbackList();

    }
}

/**
 * This method will start the callback request to the server, requesting
 * an update whenever the server sees that files have been modified.
 *
 * We're making use of a template function here, so that we can keep some
 * of the more intricate workings of the async process out of the way, and
 * give you a chance to focus more on the project's requirements.
 */
void DFSClientNodeP2::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}

//
// STUDENT INSTRUCTION:
//
// Add any additional code you need to here
//


