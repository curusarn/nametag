// This file is part of NameTag <http://github.com/ufal/nametag/>.
//
// Copyright 2016 Institute of Formal and Applied Linguistics, Faculty of
// Mathematics and Physics, Charles University in Prague, Czech Republic.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <fstream>
#include <sstream>

#include "common.h"
#include "nametag_service.h"
#include "utils/iostreams.h"
#include "utils/options.h"
#include "utils/parse_int.h"
#include "version/version.h"

using namespace ufal::nametag;

// On Linux define streambuf writing to syslog
#ifdef __linux__
#include <streambuf>
#include <syslog.h>
#include <unistd.h>

class syslog_streambuf : public streambuf {
 public:
  virtual ~syslog_streambuf() {
    syslog_write();
  }

 protected:
  virtual int overflow(int c) override {
    if (c != EOF && c != '\n')
      buffer.push_back(c);
    else
      syslog_write();
    return c;
  }

 private:
  string buffer;

  void syslog_write() {
    if (!buffer.empty()) {
      syslog(LOG_ERR, "%s", buffer.c_str());
      buffer.clear();
    }
  }
};
#endif

microrestd::rest_server server;
nametag_service service;

int main(int argc, char* argv[]) {
  iostreams_init();

  options::map options;
  if (!options::parse({{"daemon",options::value::none},
                       {"version", options::value::none},
                       {"log", options::value::any},
                       {"help", options::value::none}}, argc, argv, options) ||
      options.count("help") ||
      ((argc < 2 || (argc % 3) != 2) && !options.count("version")))
    runtime_failure("Usage: " << argv[0] << " [options] port (model_name model_file acknowledgements)*\n"
                    "Options: --daemon\n"
                    "         --log=path to log file\n"
                    "         --version\n"
                    "         --help");
  if (options.count("version")) {
    ostringstream other_libraries;
    auto microrestd = microrestd::version::current();
    other_libraries << "MicroRestD " << microrestd.major << '.' << microrestd.minor << '.' << microrestd.patch;
    return cout << version::version_and_copyright(other_libraries.str()) << endl, 0;
  }

  // Process options
  int port = parse_int(argv[1], "port number");

#ifndef __linux__
  if (options.count("daemon")) runtime_failure("The --daemon option is currently supported on Linux only!");
#endif

  // Initialize the service
  vector<nametag_service::model_description> models;
  for (int i = 2; i < argc; i += 3)
    models.emplace_back(argv[i], argv[i + 1], argv[i + 2]);

  if (!service.init(models))
    runtime_failure("Cannot load specified models!");

  // Open log file
  string log_file_name = options.count("log") ? options["log"] : string(argv[0]) + ".log";
  ofstream log_file(log_file_name.c_str(), ofstream::app);
  if (!log_file) runtime_failure("Cannot open log file '" << log_file_name << "' for writing!");

  // Daemonize if requested
#ifdef __linux__
  if (options.count("daemon")) {
    if (daemon(1, 0) != 0)
      runtime_failure("Cannot daemonize in '" << argv[0] << "' executable!");

    // Redirect cerr to syslog
    openlog("nametag_server", 0, LOG_USER);
    static syslog_streambuf syslog;
    cerr.rdbuf(&syslog);
  }
#endif

  // Start the server
  server.set_log_file(&log_file);
  //server.set_max_connections(256);
  server.set_max_request_body_size(1<<20);
  server.set_min_generated(32 * (1<<10));
  server.set_threads(100);
  server.set_timeout(60);

  if (!server.start(&service, port))
    runtime_failure("Cannot start nametag_server'!");

  cerr << "Successfully started nametag_server on port " << port << "." << endl;

  // Wait until finished
  server.wait_until_signalled();
  server.stop();

  return 0;
}
