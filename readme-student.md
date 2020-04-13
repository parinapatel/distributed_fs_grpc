
# CS6200 Distributed File System using GRPC
Parin Patel : ppatel480@gatech.edu

## Project Description

In this project, We are designing and implementing a simple distributed file system (DFS).  First, we will develop several file transfer protocols using gRPC and Protocol Buffers. Next, we are incorporating a weakly consistent synchronization system to manage cache consistency between multiple clients and a single server. Our Distributed file system supports following commands.

- Fetch : It will pull the file from remote server if file isnt locked.
- Store : It will try to store file if it has file lock.
- List : Lists all file on server.
- Stats : It returns stat object from server , which contains information about size , modified time , creation time , file path etc
- Lock : It hasnt exposed from command line but can be used to acquire write lock on file.
- Mount : It mounts remote file system on localserver , it supports write,deletion or remote changes to be reflected on local and remote system. Similar to sync operation. 
- Delete : It deletes server/remote file as well as file on all other mount clients.

Source code using a combination of C++14, gRPC, and Protocol Buffers to complete the implementation.

# Part 1 Description :

Part 1 implements following service into our server , we have mentioned this [above](#Project Description). :
- Fetch 
- Store
- List 
- Stats
- Delete

## Design: 
We will be using IFStreams/OFStreams during given project as client is single threaded & in future requirement only one client can acquire write lock on file . Thus all write operations are atomic. Secondly , each client/Server thread performing request will have its own copy of  IFStreams/OFStreams Objects copy. This will allow use non-thread safe objects in over code. More on this limitation later.
### GRPC :

We are using `stream` GRPC whenever we need to transfer file data or any large data. This allows use to reduce the memory buffer neeeded to serialize messages for GRPC. We will be following Server & Client based method to transfer the file or any required information. Client will be sending a request and either receive go ahead to transfer data or return one of the failure condition. 

### Server Side:

 - Fetch :
 1. Receive request from client.
 2. Validate if deadline isn't exceeded else return ```::grpc::StatusCode::DEADLINE_EXCEEDED``` .
 3. Translate remote path to local path.
 4. Check if file exist locally , if not send ```::grpc::StatusCode::NOT_FOUND```.
 5. Sets `file_stats` to new files stats and send payload back  .
 6. Try to Open existing and start reading/streaming the file till `EOF`.
 7. If read is successful check set  `file_transfer_status` to `FILE_TRANSFER_SUCCESS` else `FILE_TRANSFER_FAILURE` and return with with ``` ::grpc::Status::OK ```.
 
 - Store
  1. Receive request from client.
  2. Validate if deadline isn't exceeded else return ```::grpc::StatusCode::DEADLINE_EXCEEDED``` .
  3. Translate remote path to local path.
  4. Try to Open existing / Create new file. and start writing the file till `EOF` .
  5. If write is successful check set  `file_transfer_status` to `FILE_TRANSFER_SUCCESS` else `FILE_TRANSFER_FAILURE`.
  6. Sets `file_stats` to new files stats and send payload back with ``` ::grpc::Status::OK ``` .
  
 - List 
  1. Receive request from client.
  2. Validate if deadline isn't exceeded else return ```::grpc::StatusCode::DEADLINE_EXCEEDED``` .
  4. Try to Open current directory. and start reading till reaching end of list .
  5. Ignore entries which are either `.` or `..` or are hidden(starts with `.`) , otherwise start reading stats for each entry.
  6. Close the directory and send payload back with ``` ::grpc::Status::OK ``` .
 
 - Stats
  1. Receive request from client.
  2. Validate if deadline isn't exceeded else return ```::grpc::StatusCode::DEADLINE_EXCEEDED``` .
  3. Translate remote path to local path.
  4. Check if file exist locally , if not send ```::grpc::StatusCode::NOT_FOUND```.
  5. Try to Open existing and start reading stats.
  6. Sets `file_stats` to new files stats and send payload back with ``` ::grpc::Status::OK ``` .
 
 - Delete
  1. Receive request from client.
  2. Validate if deadline isn't exceeded else return ```::grpc::StatusCode::DEADLINE_EXCEEDED``` .
  3. Translate remote path to local path.
  4. Check if file exist locally , if not send ```::grpc::StatusCode::NOT_FOUND```.
  5. Try to delete existing file.
  6. send payload back with ``` ::grpc::Status::OK ``` .
  
### Client Side

 - Fetch :
 1. Creates context for server and sets deadline.
 2. Sets request path and sends payload .
 3. Open local file for writing and start writing streaming payload till `EOF` .
 4. If write is successful and  `file_transfer_status` == `FILE_TRANSFER_SUCCESS` then return `OK` else return with incoming return code.
 
 - Store
 1. Creates context for server and sets deadline.
 2. Sets request path, stats and sends payload .
 3. Open local file for writing and start reading streaming payload till `EOF` .
 4. If read is successful and incoming response  `file_transfer_status` == `FILE_TRANSFER_SUCCESS` then return `OK` else return with incoming return code.
  
 - List 
 1. Creates context for server and sets deadline and send it.
 2. sets file_map with required fields (file path and mtime). 
 3. return with incoming return code.
  
 - Stats
 1. Creates context for server and sets deadline.
 2. Sets request path and sends payload .
 3. Update / memcopy to *file_status.
 4. Return with  incoming code.
 
 - Delete
 1. Creates context for server and sets deadline.
 2. Sets request path and sends payload .
 4. Return with incoming code.
   
## Message and Method Structure:
GRPC Protos:
```c++
service DFSService {
    //  A method to store files on the server
        rpc store_file(stream  file_stream) returns (file_response);
    //  A method to fetch files from the server
        rpc fetch_file(file_request) returns (stream file_stream);
    //  A method to delete files from the server
        rpc delete_file(file_request) returns (file_response);
    // A method to list all files on the server
        rpc list_file(empty) returns (stream file_list);
     // A method to get the status of a file on the server
        rpc stat_file(file_request) returns (file_response);
}

// Response for Fetch or Request for Store
message file_stream{
        string file_name = 1;
        bytes file_stats = 2;
        bytes file_data = 3;
};

message file_request{
        string file_name = 1;
};

message file_response{
        string file_name = 1;
        int32 file_transfer_status = 2;
        bytes file_stats = 3;
};

message file_list{
        bytes files = 1;
}

message empty {

}
```


## Tests:

// TODO

# Part 2: 



### Problems & Improvements:




#### Improvements in Problem Statement

We will manually review your file looking for:

- A summary description of your project design.  If you wish to use grapics, please simply use a URL to point to a JPG or PNG file that we can review

- Any additional observations that you have about what you've done. Examples:
	- __What created problems for you?__
	- __What tests would you have added to the test suite?__
	- __If you were going to do this project again, how would you improve it?__
	- __If you didn't think something was clear in the documentation, what would you write instead?__

## Known Bugs/Issues/Limitations

__Please tell us what you know doesn't work in this submission__

## References

__Please include references to any external materials that you used in your project__

