#include <string>
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include "socket.hh"
#include "sqlite3.h"
#include "gason.h"
#include "sql_message.hh"

int handle_sql(const std::string& sql, std::string &json);
int handle_client(socket_t& socket);

/////////////////////////////////////////////////////////////////////////////////////////////////////
//usage
/////////////////////////////////////////////////////////////////////////////////////////////////////

void usage()
{
  std::cout << "-h: help, exit" << std::endl;
  std::cout << "-o PORT: port (default 3000)" << std::endl;
  exit(0);
}

///////////////////////////////////////////////////////////////////////////////////////
//main
///////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
  const char *buf_server = "127.0.0.1";
  unsigned short port = 3000;

  for (int idx = 1; idx < argc && argv[idx][0] == '-'; idx++)
  {
    switch (argv[idx][1])
    {
    case 'h':
      usage();
      break;
    case 'o':
      port = atoi(argv[idx + 1]);
      idx++;
      break;
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //server
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  tcp_server_t server(port);
  std::cout << "server: listening on port " << port << std::endl;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //server loop
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  while (true)
  {
    socket_t socket = server.accept_client();

    // convert IP addresses from a dots-and-number string to a struct in_addr
    char *str_ip = inet_ntoa(socket.m_sockaddr_in.sin_addr);
    std::cout << prt_time() << "server: accepted: " << str_ip << "," << socket.m_socket_fd << std::endl;

    if (handle_client(socket) < 0)
    {

    }
    socket.close_socket();
  }
  server.close_socket();
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//handle_client
/////////////////////////////////////////////////////////////////////////////////////////////////////

int handle_client(socket_t& socket)
{
  std::string header;
  std::string json; //response
  char buf[4096];

  if (socket.parse_http_headers(header) < 0)
  {
    std::cout << "parse_http_headers error\n";
    return -1;
  }

  std::string method = http_get_method(header);

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //GET method
  //send back JSON response
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if (method.compare("GET") == 0)
  {
    size_t start = header.find("/");
    size_t end = header.find(" ", start);
    std::string action = header.substr(start + 1, end - start - 1);
    std::cout << "REST method: " << action << "\n";
    std::string sql;
    if (action.compare("items") == 0)
    {
      sql = "SELECT * FROM table_items ;";
    }
    else if (action.compare("places") == 0)
    {
      sql = "SELECT * FROM table_places ;";
    }
    if (handle_sql(sql, json) == SQLITE_ERROR)
    {
      return -1;
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //POST method
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  else if (method.compare("POST") == 0)
  {
    unsigned long long size_body = http_get_field("Content-Length: ", header);
    std::cout << "received: Content-Length: " << size_body << std::endl;
    if (size_body == 0)
    {
      std::string response("HTTP/1.1 200 OK\r\n\r\n");
      response += "<html><body>Invalid request</body></html>";
      if (socket.write_all(response.c_str(), response.size()) < 0)
      {
        std::cout << "write response error\n";
      }
      return 0;
    }

    //now get body using size of Content-Length
    if (socket.read_all(buf, (int)size_body) < 0)
    {
      std::cout << "recv error: " << strerror(errno) << std::endl;
      return -1;
    }

    char *endptr;
    JsonValue value;
    JsonAllocator allocator;
    int status = jsonParse(buf, &endptr, &value, allocator);
    if (status != JSON_OK)
    {
      std::cout << "invalid JSON format for " << buf << std::endl;
      return -1;
    }

    std::cout << std::endl << "Parsing SQL in JSON..." << std::endl << std::endl;

    //JSON is an array of strings, each string is a SQL statement
    std::vector<std::string> vec_sql;
    for (JsonNode *node = value.toNode(); node != nullptr; node = node->next)
    {
      vec_sql.push_back(node->value.toString());
    }

    std::cout << std::endl << "Executing SQL to database..." << std::endl << std::endl;

    size_t nbr_sql = vec_sql.size();
    for (size_t idx = 0; idx < nbr_sql; idx++)
    {
      if (handle_sql(vec_sql.at(idx), json) == SQLITE_ERROR)
      {
        return -1;
      }
    }
  }
  else
  {
    std::cout << "invalid HTTP header method: " << method.c_str() << "\n";
    return -1;
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //response
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  std::string response("HTTP/1.1 200 OK\r\n");
  response += "Content-Type: application/json\r\n";
  response += "Content-Length: ";
  response += std::to_string(json.size());
  response += "\r\n";
  response += "\r\n"; //terminate HTTP headers
  response += json;
  if (socket.write_all(response.c_str(), response.size()) < 0)
  {
    std::cout << "write response error\n";
  }
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//handle_sql
/////////////////////////////////////////////////////////////////////////////////////////////////////

int handle_sql(const std::string& sql, std::string &json)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  std::cout << "SQL:" << std::endl;
  std::cout << sql.c_str() << std::endl;

  if (sqlite3_open("test.sqlite", &db) != SQLITE_OK)
  {
    std::cout << sqlite3_errmsg(db);
    return SQLITE_ERROR;
  }

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
  {
    std::cout << sqlite3_errmsg(db);
    sqlite3_close(db);
    return SQLITE_ERROR;
  }

  std::vector<std::string> vec;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
  {
    const unsigned char *field_0 = sqlite3_column_text(stmt, 0);
    const unsigned char *field_1 = sqlite3_column_text(stmt, 1);
    int field_2 = sqlite3_column_int(stmt, 2);

    //unfortunately, sqlite3_column_int returns integer 0 for error

    std::string str((char*)field_0);
    str += " ";
    str += (char*)field_1;
    vec.push_back(str);
    std::cout << str.c_str() << std::endl;
  }

  //make JSON reply (an array of strings)
  json = "[";
  for (int idx = 0; idx < vec.size(); idx++)
  {
    json += "\"";
    json += vec.at(idx);
    json += "\"";
    if (idx < vec.size() - 1)
    {
      json += ",";
    }
  }
  json += "]";

  std::ofstream ofs("response.json");
  ofs << json.c_str();
  ofs.close();

  if (sqlite3_finalize(stmt) != SQLITE_OK)
  {
    std::cout << sqlite3_errmsg(db) << std::endl;;
    sqlite3_close(db);
    return SQLITE_ERROR;
  }
  sqlite3_close(db);
  return SQLITE_OK;
}

