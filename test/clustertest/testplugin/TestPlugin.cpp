#include "TestPlugin.h"

extern "C" void BEDROCK_PLUGIN_REGISTER_TESTPLUGIN() {
    // Register the global instance
    new BedrockPlugin_TestPlugin();
}

BedrockPlugin_TestPlugin::BedrockPlugin_TestPlugin() :
  _server(nullptr)
{ }

void BedrockPlugin_TestPlugin::initialize(const SData& args, BedrockServer& server) {
    if (httpsManagers.empty()) {
        httpsManagers.push_back(&httpsManager);
    }
    _server = &server;
}

bool BedrockPlugin_TestPlugin::peekCommand(SQLite& db, BedrockCommand& command) {
    // This should never exist when calling peek.
    SASSERT(!command.httpsRequest);
    if (command.request.methodLine == "testcommand") {
        command.response.methodLine = "200 OK";
        command.response.content = "this is a test response";
        return true;
    } else if (command.request.methodLine == "sendrequest") {
        if (_server->getState() != SQLiteNode::MASTERING) {
            // Only start HTTPS requests on master, otherwise, we'll escalate.
            return false;
        }
        SData request("GET / HTTP/1.1");
        request["Host"] = "www.expensify.com";
        command.request["httpsRequests"] = to_string(command.request.calc("httpsRequests") + 1);
        command.httpsRequest = httpsManager.send("https://www.expensify.com/", request);
        return false; // Not complete.
    }

    return false;
}

bool BedrockPlugin_TestPlugin::processCommand(SQLite& db, BedrockCommand& command) {
    if (command.request.methodLine == "sendrequest") {
        if (command.httpsRequest) {
            // If we're calling `process` on a command with a https request, it had better be finished.
            SASSERT(command.httpsRequest->finished);
            command.response.methodLine = to_string(command.httpsRequest->response);
            // return the number of times we made an HTTPS request on this command.
            int tries = SToInt(command.request["httpsRequests"]);
            if (tries != 1) {
                STHROW("500 Retried HTTPS request!");
            }
            command.response.content = " " + command.httpsRequest->fullResponse.content;

            // Update the DB so we can test conflicts.
            if (!command.request["Query"].empty()) {
                if (!db.write(command.request["Query"])) {
                    STHROW("502 Query failed.");
                }
            }
        } else {
            // Shouldn't get here.
            SASSERT(false);
        }
        return true;
    }
    else if (command.request.methodLine == "idcollision") {
        SQResult result;
        db.read("SELECT MAX(id) FROM test", result);
        SASSERT(result.size());
        int nextID = SToInt(result[0][0]) + 1;
        SASSERT(db.write("INSERT INTO TEST VALUES(" + SQ(nextID) + ", " + SQ(command.request["value"]) + ");"));
        return true;
    }
    return false;
}

void BedrockPlugin_TestPlugin::upgradeDatabase(SQLite& db) {
    bool ignore;
    SASSERT(db.verifyTable("dbupgrade", "CREATE TABLE dbupgrade ( "
                                        "id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                                        "value )", ignore));
}


bool TestHTTPSMananager::_onRecv(Transaction* transaction) {
    string methodLine = transaction->fullResponse.methodLine;
    transaction->response = 0;
    size_t offset = methodLine.find_first_of(' ', 0);
    offset = methodLine.find_first_not_of(' ', offset);
    if (offset != string::npos) {
        int status = SToInt(methodLine.substr(offset));
        if (status) {
            transaction->response = status;
        }
    }
    if (!transaction->response) {
        transaction->response = 400;
        SWARN("Failed to parse method line from request: " << methodLine);
    }

    return false;
}

TestHTTPSMananager::~TestHTTPSMananager() {
}

TestHTTPSMananager::Transaction* TestHTTPSMananager::send(const string& url, const SData& request) {
    return _httpsSend(url, request);
}
