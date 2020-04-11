#include <map>
#include <mutex>
#include <shared_mutex>
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

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/dfslibx-call-data.h"
#include "src/dfslibx-service-runner.h"
#include "dfslib-shared-p2.h"
#include "dfslib-servernode-p2.h"

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
// Change these "using" aliases to the specific
// message types you are using in your `dfs-service.proto` file
// to indicate a file request and a listing of files from the server
//
using FileRequestType = dfs_service::empty;
using FileListResponseType = dfs_service::file_list;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// As with Part 1, the DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in your `dfs-service.proto` file.
//
// You may start with your Part 1 implementations of each service method.
//
// Elements to consider for Part 2:
//
// - How will you implement the write lock at the server level?
// - How will you keep track of which client has a write lock for a file?
//      - Note that we've provided a preset client_id in DFSClientNode that generates
//        a client id for you. You can pass that to the server to identify the current client.
// - How will you release the write lock?
// - How will you handle a store request for a client that doesn't have a write lock?
// - When matching files to determine similarity, you should use the `file_checksum` method we've provided.
//      - Both the client and server have a pre-made `crc_table` variable to speed things up.
//      - Use the `file_checksum` method to compare two files, similar to the following:
//
//          std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);
//
//      - Hint: as the crc checksum is a simple integer, you can pass it around inside your message types.
//
class DFSServiceImpl final :
    public DFSService::WithAsyncMethod_CallbackList<DFSService::Service>,
        public DFSCallDataManager<FileRequestType , FileListResponseType> {

private:

    /** The runner service used to start the service and manage asynchronicity **/
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex for managing the queue requests **/
    std::mutex queue_mutex;

    /** The vector of queued tags used to manage asynchronous requests **/
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;


    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    /** CRC Table kept in memory for faster calculations **/
    CRC::Table<std::uint32_t, 32> crc_table;


    std::map<std::string, std::string> lock_file_map{};

    bool check_for_file_lock(std::string file_path, std::string client_id) {
        if (lock_file_map.count(file_path) == 0) {
            return UNLOCKED;
        }
        if (lock_file_map[file_path] == client_id) {
            return UNLOCKED;
        }
        return LOCKED;
    }

    bool acquire_file_lock(std::string file_path, std::string client_id) {
        if (lock_file_map.count(file_path) != 0) {
            lock_file_map[file_path] = client_id;
            return LOCKED;
        } else return UNLOCKED;
    }

    std::string find_lock_client(std::string file_path) {
        if (lock_file_map.count(file_path) != 0) {
            return "";
        } else return lock_file_map[file_path];
    }

    bool unlock_file_lock(std::string file_path) {
        if (lock_file_map.count(file_path) == 0) {
            return UNLOCKED;
        }
        if (lock_file_map.erase(file_path) > 0) return UNLOCKED;
        return LOCKED;
    }


public:

    DFSServiceImpl(const std::string &mount_path, const std::string &server_address, int num_async_threads) :
            mount_path(mount_path), crc_table(CRC::CRC_32()) {

        this->runner.SetService(this);
        this->runner.SetAddress(server_address);
        this->runner.SetNumThreads(num_async_threads);
        this->runner.SetQueuedRequestsCallback([&] { this->ProcessQueuedRequests(); });

    }

    ~DFSServiceImpl() {
        this->runner.Shutdown();
    }

    void Run() {
        this->runner.Run();
    }

    /**
     * Request callback for asynchronous requests
     *
     * This method is called by the DFSCallData class during
     * an asynchronous request call from the client.
     *
     * Students should not need to adjust this.
     *
     * @param context
     * @param request
     * @param response
     * @param cq
     * @param tag
     */
    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) {

        std::lock_guard<std::mutex> lock(queue_mutex);
        this->queued_tags.emplace_back(context, request, response, cq, tag);

    }

    /**
     * Process a callback request
     *
     * This method is called by the DFSCallData class when
     * a requested callback can be processed. You should use this method
     * to manage the CallbackList RPC call and respond as needed.
     *
     * See the STUDENT INSTRUCTION for more details.
     *
     * @param context
     * @param request
     * @param response
     */
    void ProcessCallback(ServerContext* context, FileRequestType* request, FileListResponseType* response) {

        //
        // STUDENT INSTRUCTION:
        //
        // You should add your code here to respond to any CallbackList requests from a client.
        // This function is called each time an asynchronous request is made from the client.
        //
        // The client should receive a list of files or modifications that represent the changes this service
        // is aware of. The client will then need to make the appropriate calls based on those changes.
        //
        CallbackList(context, request, response);

    }

    /**
     * Processes the queued requests in the queue thread
     */
    void ProcessQueuedRequests() {
        while(true) {

            //
            // STUDENT INSTRUCTION:
            //
            // You should add any synchronization mechanisms you may need here in
            // addition to the queue management. For example, modified files checks.
            //
            // Note: you will need to leave the basic queue structure as-is, but you
            // may add any additional code you feel is necessary.
            //


            // Guarded section for queue
            {
                dfs_log(LL_DEBUG2) << "Waiting for queue guard";
                std::lock_guard<std::mutex> lock(queue_mutex);


                for(QueueRequest<FileRequestType, FileListResponseType>& queue_request : this->queued_tags) {
                    this->RequestCallbackList(queue_request.context, queue_request.request,
                        queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                    queue_request.finished = true;
                }

                // any finished tags first
                this->queued_tags.erase(std::remove_if(
                    this->queued_tags.begin(),
                    this->queued_tags.end(),
                    [](QueueRequest<FileRequestType, FileListResponseType>& queue_request) { return queue_request.finished; }
                ), this->queued_tags.end());
            }
        }
    }

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // the implementations of your rpc protocol methods.
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
        response->set_file_name(file_content.file_name());
        response->set_client_id(file_content.client_id());
        std::string file_name = WrapPath(file_content.file_name());
        if (check_for_file_lock(file_name, file_content.client_id()) == UNLOCKED) {
            if (acquire_file_lock(file_name, file_content.client_id()) != LOCKED) {
                response->set_file_lock(LOCKED);
                response->set_file_transfer_status(FILE_SERVER_EXAUSTED);
                dfs_log(LL_DEBUG) << "Given file ; " + file_content.file_name() + " is locked by client : "
                                  << find_lock_client(file_name);
                return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                      "Given file ; " + file_content.file_name() + " is locked by another client.");
            }
        }


        if (check_file_content(file_name, &this->crc_table, response->file_crc()) == FILE_EXIST)
            return ::grpc::Status(::grpc::StatusCode::ALREADY_EXISTS,
                                  "file " + file_content.file_name() + " already exist on server");


        if (write_to_file(file_name, reader) == -1) {
            dfs_log(LL_ERROR) << "Write To file Failed.";
            if (unlock_file_lock(file_name) != UNLOCKED)
                return ::grpc::Status(::grpc::StatusCode::INTERNAL, "UNLOCK FAILED.");
            response->set_file_transfer_status(FILE_TRANSFER_FAILURE);
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Write of File Failed.");
        }

        dfs_log(LL_DEBUG3) << "Successfully Written the File and Getting Stats";
        response->set_file_transfer_status(FILE_TRANSFER_SUCCESS);
        response->set_file_crc(dfs_file_checksum(file_name, &this->crc_table));
        struct stat file_status = get_file_stats(file_name, response);
        response->set_file_name(file_name);
        response->set_file_stats(&file_status, sizeof(file_status));

        dfs_log(LL_DEBUG3) << "Successfully Sent the resposne and sending reply";
        if (unlock_file_lock(file_name) != UNLOCKED)
            return ::grpc::Status(::grpc::StatusCode::INTERNAL, "UNLOCK FAILED.");
        else
            return ::grpc::Status::OK;

    }


    // 2. REQUIRED (Parts 1 & 2): A method to fetch files from the server
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

        ::dfs_service::file_response temp;
        struct stat temp_file_stats = get_file_stats(file_name, &temp);
        if (temp.file_transfer_status() == FILE_TRANSFER_FAILURE) {
            dfs_log(LL_ERROR) << "Write To file Failed.";
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Write of File Failed.");
        } else {

            file_content.set_file_stats(&temp_file_stats, sizeof(temp_file_stats));
            file_content.set_file_crc(dfs_file_checksum(file_name, &this->crc_table));

            writer->Write(file_content);

            if (check_for_file_lock(file_name, request->client_id()) == LOCKED) {
                dfs_log(LL_DEBUG) << "Given file ; " + file_content.file_name() + " is locked by client : "
                                  << find_lock_client(file_name);
                return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                      "Given file ; " + file_content.file_name() + " is locked by another client.");
            } else {
                if (read_file(file_name, writer) == -1) {
                    dfs_log(LL_ERROR) << "Write To file Failed.";
                    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Write of File Failed.");
                }
            }

        }
        return ::grpc::Status::OK;

    }

    // 3. REQUIRED (Parts 1 & 2): A method to list all files on the server
    ::grpc::Status list_file(::grpc::ServerContext *context, const ::dfs_service::empty *request,
                             ::grpc::ServerWriter<::dfs_service::file_list> *writer) override {
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }
        DIR *current_dir;
        struct file_object temp_file_object{};


        struct dirent *entry;

        current_dir = opendir(mount_path.c_str());
        if (current_dir == nullptr) {
            dfs_log(LL_ERROR) << "Error in Opening dir: " << mount_path << strerror(errno);
            return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                                  "Error in Opening dir: " + mount_path + strerror(errno));
        }
        struct stat temp_stat{};
        ::dfs_service::file_list fileList;

        while ((entry = readdir(current_dir)) != nullptr) {
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0 &&
                entry->d_name[0] != '.') {
                stat(WrapPath(entry->d_name).c_str(), &temp_stat);
                strcpy(temp_file_object.file_path, entry->d_name);
                temp_file_object.mtime = temp_stat.st_mtim.tv_sec;
                dfs_log(LL_DEBUG3) << temp_file_object.file_path;

                fileList.set_files(&temp_file_object, sizeof(struct file_object));
                writer->Write(fileList);
            }
        }
        closedir(current_dir);
        return ::grpc::Status::OK;

    }

    // 4. REQUIRED (Parts 1 & 2): A method to get the status of a file on the server
    ::grpc::Status stat_file(::grpc::ServerContext *context, const ::dfs_service::file_request *request,
                             ::dfs_service::file_response *response) override {
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }

        std::string file_path = WrapPath(request->file_name());
        struct stat file_stats{};
        if (check_for_file_lock(file_path, request->client_id()) == LOCKED) {
            dfs_log(LL_DEBUG) << "Given file ; " + request->file_name() + " is locked by client : "
                              << find_lock_client(file_path);
            return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                  "Given file ; " + request->file_name() + " is locked by another client.");
        }
        file_stats = get_file_stats(file_path, response);
        response->set_file_name(file_path);
        response->set_file_stats(&file_stats, sizeof(struct stat));

        if (response->file_transfer_status() == FILE_TRANSFER_FAILURE) {
            return ::grpc::Status(StatusCode::NOT_FOUND, "FIle Not found.");
        }
        return ::grpc::Status::OK;

    }


    // 5. REQUIRED (Part 2 only): A method to request a write lock from the server
    ::grpc::Status lock_file(::grpc::ServerContext *context, const ::dfs_service::file_request *request,
                             ::dfs_service::file_response *response) override {
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }
        std::string file_path = WrapPath(request->file_name());

        if (check_for_file_lock(file_path, request->client_id()) == UNLOCKED) {
            if (acquire_file_lock(file_path, request->client_id()) != LOCKED) {
//                response->set_file_lock(LOCKED);
                response->set_file_transfer_status(FILE_SERVER_EXAUSTED);
                dfs_log(LL_DEBUG) << "Given file ; " + request->file_name() + " is locked by client : "
                                  << find_lock_client(file_path);
                return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                      "Given file ; " + request->file_name() + " is locked by another client.");
            }
        }
        response->set_file_name(request->file_name());
        response->set_file_lock(LOCKED);
        response->set_client_id(request->client_id());
        return ::grpc::Status::OK;
    }


    // 6. REQUIRED (Part 2 only): A method named CallbackList to handle asynchronous file listing requests
    //                            from a client. This method should return a listing of files along with their
    //                            attribute information. The expected attribute information should include name,
    //                            size, modified time, and creation time.
    ::grpc::Status CallbackList(::grpc::ServerContext *context, const ::dfs_service::empty *request,
                                ::dfs_service::file_list *response) override {
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }

        DIR *current_dir;
        struct file_object temp_file_object{};
        std::vector<struct file_object> file_storage_list{};

        struct dirent *entry;

        current_dir = opendir(mount_path.c_str());
        if (current_dir == nullptr) {
            dfs_log(LL_ERROR) << "Error in Opening dir: " << mount_path << strerror(errno);
            return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                                  "Error in Opening dir: " + mount_path + strerror(errno));
        }
        struct stat temp_stat{};
        ::dfs_service::file_list fileList;

        while ((entry = readdir(current_dir)) != nullptr) {
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0 &&
                entry->d_name[0] != '.') {
                stat(WrapPath(entry->d_name).c_str(), &temp_stat);
                strcpy(temp_file_object.file_path, entry->d_name);
                temp_file_object.mtime = temp_stat.st_mtim.tv_sec;
                temp_file_object.create_time = temp_stat.st_ctim.tv_sec;
                temp_file_object.file_size = temp_stat.st_size;
                temp_file_object.file_crc = dfs_file_checksum(temp_file_object.file_path, &this->crc_table);

                dfs_log(LL_DEBUG3) << temp_file_object.file_path;
                file_storage_list.push_back(temp_file_object);
            }
        }
        closedir(current_dir);


        response->set_file_length(file_storage_list.size());
        struct file_object temp_arr[file_storage_list.size()];
        std::copy(file_storage_list.begin(), file_storage_list.end(), temp_arr);
        response->set_files(&temp_arr, sizeof(temp_arr));
        return ::grpc::Status::OK;
    }


    // 7. REQUIRED (Part 2 only): A method to delete a file from the server
    ::grpc::Status delete_file(::grpc::ServerContext *context, const ::dfs_service::file_request *request,
                               ::dfs_service::file_response *response) override {
        if (context->IsCancelled()) {
            dfs_log(LL_DEBUG2) << "Context is cancelled or Deadline Exceeded.";
            return ::grpc::Status(::grpc::StatusCode::DEADLINE_EXCEEDED,
                                  "Context is cancelled or Deadline Exceeded or Timeout");
        }

        ::std::string file_name = WrapPath(request->file_name());
        response->set_file_name(file_name);
        if (check_for_file_lock(file_name, request->client_id()) == LOCKED) {
            dfs_log(LL_DEBUG) << "Given file ; " + request->file_name() + " is locked by client : "
                              << find_lock_client(file_name);
            return ::grpc::Status(::grpc::StatusCode::RESOURCE_EXHAUSTED,
                                  "Given file ; " + request->file_name() + " is locked by another client.");
        }
        if (remove(file_name.c_str()) == -1) {
            response->set_file_transfer_status(FILE_TRANSFER_FAILURE);
            dfs_log(LL_ERROR) << "File Deletion Failed for File : " << file_name << strerror(errno);
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "File Deletion Failed.");
        } else response->set_file_transfer_status(FILE_TRANSFER_SUCCESS);

        return ::grpc::Status::OK;
    }


};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly
// to add additional startup/shutdown routines inside, but be aware that
// the basic structure should stay the same as the testing environment
// will be expected this structure.
//
/**
 * The main server node constructor
 *
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        int num_async_threads,
        std::function<void()> callback) :
        server_address(server_address),
        mount_path(mount_path),
        num_async_threads(num_async_threads),
        grader_callback(callback) {}
/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

/**
 * Start the DFSServerNode server
 */
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);


    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    service.Run();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional definitions here
//
