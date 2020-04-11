#ifndef PR4_DFSLIB_SHARED_H
#define PR4_DFSLIB_SHARED_H

#include <algorithm>
#include <cctype>
#include <locale>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <sys/stat.h>

#include "src/dfs-utils.h"
#include "proto-src/dfs-service.grpc.pb.h"


//
// STUDENT INSTRUCTION
//
// The following defines and methods must be left as-is for the
// structural components provided to work.
//
#define DFS_RESET_TIMEOUT 3000
#define DFS_I_EVENT_SIZE (sizeof(struct inotify_event))
#define DFS_I_BUFFER_SIZE (1024 * (DFS_I_EVENT_SIZE + 16))


#define  BUFFER_SIZE 4096
#define FILE_TRANSFER_SUCCESS 200
#define FILE_TRANSFER_FAILURE 500
#define FILE_SERVER_EXAUSTED 429
#define  LOCKED true
#define  UNLOCKED false

#define  FILE_EXIST true
/** A file descriptor type **/
typedef int FileDescriptor;

/** A watch descriptor type **/
typedef int WatchDescriptor;

/** An inotify callback method **/
typedef void (*InotifyCallback)(uint, const std::string &, void *);

/** The expected struct for the inotify setup in this project **/
struct NotifyStruct {
    FileDescriptor fd;
    WatchDescriptor wd;
    uint event_type;
    std::thread * thread;
    InotifyCallback callback;
};

/** The expected event type for the event method in this project **/
struct EventStruct {
    void* event;
    void *instance;
};

//
// STUDENT INSTRUCTION:
//
// Add any additional shared code here
//


struct file_object {
    char file_path[256];
    std::int32_t mtime;
    std::uint64_t file_size;
    std::int32_t create_time;
    std::uint32_t file_crc;
};

int write_to_file(std::string filepath, ::grpc::ServerReader<::dfs_service::file_stream> *reader);

struct stat get_file_stats(std::string filepath, ::dfs_service::file_response *response);

bool check_file_content(std::string file_path, CRC::Table<std::uint32_t, 32> *table, std::uint32_t CRC);

int read_file(std::string filepath, ::grpc::ServerWriter<::dfs_service::file_stream> *writer);


#endif

