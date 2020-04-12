#include <string>
#include <iostream>
#include <fstream>
#include <cstddef>
#include <sys/stat.h>

#include "dfslib-shared-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

// Global log level used throughout the system
// Note: this may be adjusted from the CLI in
// both the client and server executables.
// This shouldn't be changed at the file level.
dfs_log_level_e DFS_LOG_LEVEL = LL_ERROR;
//dfs_log_level_e DFS_LOG_LEVEL = LL_DEBUG3;
//
// STUDENT INSTRUCTION:
//
// Add your additional code here. You may use
// the shared files for anything you need to add outside
// of the main structure, but you don't have to use them.
//
// Just be aware they are always submitted, so they should
// be compilable.
//


int write_to_file(std::string filepath, ::grpc::ServerReader<::dfs_service::file_stream> *reader) {
    std::ofstream file_writer;
    file_writer.open(filepath, std::ios::out | std::ios::binary);
    ::dfs_service::file_stream temp;
    if (file_writer.is_open()) {
        while (reader->Read(&temp)) {
            file_writer.write(temp.file_data().c_str(), temp.file_data().size());
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
    return -1;
}


struct stat get_file_stats(std::string filepath, ::dfs_service::file_response *response) {
    struct stat file_stat{};
    if (stat(filepath.c_str(), &file_stat) == -1) {
        dfs_log(LL_ERROR) << "File Stats Failed for file." << filepath << strerror(errno);
        response->set_file_transfer_status(FILE_TRANSFER_FAILURE);
    } else response->set_file_transfer_status(FILE_TRANSFER_SUCCESS);
    return file_stat;
}

bool check_file_content(std::string file_path, CRC::Table<std::uint32_t, 32> *table, std::uint32_t CRC) {
    std::uint32_t server_crc = dfs_file_checksum(file_path, table);
    dfs_log(LL_DEBUG) << "CURRENT_CRC: " << server_crc << "\t incoming CRC : " << CRC;
    if (server_crc == CRC) {
        return FILE_EXIST;
    } else return false;
}

int read_file(std::string filepath, ::grpc::ServerWriter<::dfs_service::file_stream> *writer) {
    std::ifstream file_reader;
    dfs_log(LL_DEBUG3) << "filename: " << filepath;

    file_reader.open(filepath, std::ios::in | std::ios::binary);
    ::dfs_service::file_stream temp;
    char buffer[BUFFER_SIZE];

    if (file_reader.is_open()) {
        while (file_reader.read(buffer, BUFFER_SIZE - 1)) {
            temp.set_file_data(buffer, BUFFER_SIZE - 1);
            bzero(buffer, BUFFER_SIZE);
            writer->Write(temp);
            temp.clear_file_data();
        }

// Flush Last Bites
        if (file_reader.gcount() > 0) {
            dfs_log(LL_DEBUG2) << "EOF BUFFER :  " << file_reader.gcount();
            temp.set_file_data(buffer, file_reader.gcount());
            writer->Write(temp);
            temp.clear_file_data();
        }

        temp.clear_file_data();
        file_reader.close();
        return 0;
    } else {
        dfs_log(LL_ERROR) << "File Opening Failed" << filepath;
        return -1;
    }
}