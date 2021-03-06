#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netdb.h>

using namespace std;

/********* Constants **********/

//! Buffer Length.
#define BUFFER_LEN  10240
//! Carraige Return charachter.
#define CR          ((char) 13)
//! Line Feed charachter.
#define LF          ((char) 10)
//! CRLF String.
#define CRLF        string(1, CR) + string(1, LF)
#define TAB         "\t"
//! End of Content.
#define EOC         "." + CRLF
#define MY_HOST     "0.0.0.0"
#define MY_PORT     7070

/********* Class **********/

/**
 * Class for FileGopherServer.
 */
class FileGopherServer {
    //! Socket File descriptoe and the port.
    int socket_fd, socket_port;
    char buffer[BUFFER_LEN];
    //! Map to hold routes.
    unordered_map<string, string> routes;
    struct sockaddr_in server_addr, client_addr;
    string root;
    string socket_host;
    static void error(const char *msg);
    static bool is_regular_file(const char *path);
    static bool is_text_file(const char *file);
    static char get_file_type(const char *file);
    void send_file_contents(int client_fd, const char *file);
    int get_contents(char buffer[], int size);
    public:
    static string create_line(char type, string user_setting, 
        string path = "", string host = "error.host", int port = 1);
    FileGopherServer(string root, int port);
    FileGopherServer();
    void start(int n_connections = 5);
    void add_route(string route, string content);
};

/********* Private Functions **********/

/**
 * Function to put error on stdout
 * @param msg The error message.
 */
void FileGopherServer::error(const char *msg) {
    perror(msg);
    exit(1);
}

/**
 * Function to check if the given path is a 
 * regular file or directory.
 * @param path The path to file.
 * @return True if path is a regular file.
 */
bool FileGopherServer::is_regular_file(const char *path) {
    // Get status of path
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

/**
 * Function to check if file is text file or binary.
 * @param file The path to file.
 * @return True if file is text file.
 */
bool FileGopherServer::is_text_file(const char *file) {
    // TODO: This method is too slow, come up with 
    //       something efficient.
    char cmd[256];
    // Get mime type of file and check if it is 
    // text/*.
    sprintf(cmd, 
        "if file -i \"%s\" | grep -q text; \
        then echo 't'; else echo 'f'; fi", file);
    FILE *output = popen(cmd, "r");
    if(!output)
        error("Error determing type of file");
    char buffer[10];
    char *line_p = fgets(buffer, sizeof(buffer), output);
    pclose(output);
    if(buffer[0] == 't') 
        return true;
    return false;
}

/**
 * Function to get Gopher type of file.
 * @param file The path to file.
 * @return The Gopher type of file.
 */
char FileGopherServer::get_file_type(const char *file){
    if(is_text_file(file))
        return '0';
    // Binary file
    return '9';
}

/**
 * Function to send the given file over socket.
 * @param client_fd The file descriptor of client's socket.
 * @param file The path to file.
 */
void FileGopherServer::send_file_contents(int client_fd, const char *file) {
    // TODO: This method first reads whole file and
    //       writes over socket. Implement simultaneous 
    //       read and write.
    int fd = open(file , O_RDONLY);
	printf("sending file %s\n", file);
	struct stat s;
	fstat(fd, &s); // i get the size
	void *adr = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0); // i get the adress
	write(client_fd, adr, s.st_size); // i send the file from this adress directly
}

/**
 * Function to get contents of selector on the server.
 * The selector is contained in buffer.
 * The contents are also placed in buffer and size of 
 * contents is returned.
 * @param buffer The selector.
 * @param size The size of selector.
 * @return The size of contents.
 */
int FileGopherServer::get_contents(char buffer[], int size){
    // Remove CRLF from selector.
    if(buffer[size - 2] == CR && buffer[size - 1] == LF){
        buffer[size - 2] = '\0';
        size -= 2;
    }
    string route(buffer, buffer + size);
    // Check if selector is gopher+.
    if(buffer[0] == '\t'){
        if(routes.find(route) == routes.end()){
            strcpy(buffer, "\0");
            return 0;
        }
        strcpy(buffer, routes[route].c_str());
        return routes[route].size();
    }
    // Full path of selector.
    string dir = "";
    dir += root + route;
    printf("client requested %s\n", dir.c_str());
    // If the path is a file, send it.
    if(is_regular_file(dir.c_str())) {
        // The path is a file, so send the file contents instead
        strcpy(buffer, dir.c_str());
        return -1;
    }
    // Read the contents of directory.
    string files;
    DIR *dp;
    struct dirent *dirp;
    if((dp = opendir(dir.c_str())) == NULL) {
        cout << "Error(" << errno << ") opening " << dir << endl;
        return 0;
    }    
    while ((dirp = readdir(dp)) != NULL) {
        // Skip hidden files
        // TODO: Add parameter to show hidden files
        if(dirp->d_name[0] == '.')
            continue;
        string filename = dir + "/" + string(dirp->d_name);
        files += create_line(
            (dirp->d_type == DT_DIR) ? '1' : get_file_type(filename.c_str()),
            dirp->d_name,
            route + "/" + string(dirp->d_name),
            socket_host,
            socket_port
        );
    }
    closedir(dp);
    strcpy(buffer, files.c_str());
    return files.size();
}

/********* Public Functions **********/

/**
 * Function to create a gopher listing.
 * @param type The type of listing.
 * @param user_string The string visible to user.
 * @param path The selector to the listing.
 * @param host The host where the listing resides.
 * @param port The port where the listing resides.
 */
string FileGopherServer::create_line(char type, string user_string, 
        string path, string host, int port){
    // Convert integer port to string.
    char ch_port[6];
    sprintf(ch_port, "%d", port);
    return string(1, type) 
            + user_string + TAB 
            + path + TAB
            + host + TAB
            + string(ch_port) + CRLF;
}

/**
 * Constructor for FileGopherServer.
 * @constructor
 * @param root The root of FileGopherServer.
 * @param port The port of the server.
 */
FileGopherServer::FileGopherServer(string root, int port) {
    this->root = root;
    this->socket_port = port;
    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0)
        error("Error opening socket");
    memset((char *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *) &server_addr,
            sizeof(server_addr)) < 0) 
        error("ERROR on binding");
    // Get host of the server.
    char hostname[256];
    gethostname(hostname, 256);
    struct hostent *hostentry;
    hostentry = gethostbyname(hostname);
    char * ipbuf;
    ipbuf = inet_ntoa(*((struct in_addr *)hostentry->h_addr_list[0]));
    printf("%s\n", ipbuf);
    this->socket_host = string(ipbuf);
    // Make the server gopher+ complaint.
    string gopher_plus_content = "";
    gopher_plus_content += "+-1" + CRLF;
    gopher_plus_content += "+INFO: 1Main menu (non-gopher+)		0.0.0.0	7070" + CRLF;
    gopher_plus_content += EOC;
    add_route("\t$", gopher_plus_content);
    add_route("\t", gopher_plus_content);
}

/**
 * Non-parameter constructor
 * @constructor
 */
FileGopherServer::FileGopherServer() {
    FileGopherServer("/", 70);
}

/**
 * Function to start the server.
 * @param n_connections The number of simultaneos connections.
 *                      Default 5.
 */
void FileGopherServer::start(int n_connections){
    if(listen(socket_fd, n_connections))
        error("ERROR cannot listen");
    while(true){
        socklen_t client_len = sizeof(client_addr);
        int newsocket_fd = accept(socket_fd, 
            (struct sockaddr *) &client_addr, &client_len);
        if (newsocket_fd < 0) 
            error("ERROR on accept");
        printf("server: got connection from %s port %d\n",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        int n = read(newsocket_fd, buffer, BUFFER_LEN);
        if (n < 0) 
            error("ERROR reading from socket");
        int size = get_contents(buffer, n);
        if(size >= 0)
            send(newsocket_fd, buffer, size, 0);
        else  
            send_file_contents(newsocket_fd, buffer);
        close(newsocket_fd);
    }
}

/**
 * Function to add route to server.
 * @param route The path to route.
 * @param content The contents of route.
 */
void FileGopherServer::add_route(string route, string content) {
    routes[route] = content;
}


// Driver function.
int main(int argc, char *argv[]){
    // Print Usage.
    if(argc != 2){
        printf("USAGE: \n\tserver <directory-to-serve>\n\n");
        printf("Where <directory-to-serve> root directory of server.\n");
        return 0;
    }
    // Check if given directory can be opened.
    if(opendir(argv[1]) == NULL) {
        cout << "Error(" << errno << ") opening " << argv[1] << endl;
        return 0;
    }    
    // Creat and start FileGopherServer.
    FileGopherServer fileGopherServer(argv[1], 7070);
    fileGopherServer.start();
    return 0;
}