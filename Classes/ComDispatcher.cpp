#include "ComDispatcher.h"

SerialPort ComDispatcher::port;

Conversation::Conversation ()
{
}

Conversation::Conversation (const Conversation&)
{
}

ComDispatcher::ComDispatcher ()
{
}

ComDispatcher::~ComDispatcher ()
{
    stop_sendThread = true;
    stop_receiveThread = true;
    stop_expireThread = true;

    if(sendThread->joinable()) sendThread->join();
    if(receiveThread->joinable()) receiveThread->join();
    if(expireThread->joinable()) expireThread->join();
}

bool ComDispatcher::activate ()
{
    sendThread = new std::thread (&ComDispatcher::dispatchSendings, this);
    receiveThread = new std::thread (&ComDispatcher::dispatchReceipts, this);
    expireThread = new std::thread (&ComDispatcher::expireConversations, this);
}

unsigned int ComDispatcher::sendMessage (std::string message, std::string responseFormat, std::mutex* signal)
{
    Conversation element;

    conversations.push_back(element);
    conversations.back().ttl = 0;
    conversations.back().signal = signal;
    conversations.back().request = message;
    conversations.back().responseFormat = responseFormat;
    conversations.back().id = getNewId();

    return conversations.back().id;
}

void ComDispatcher::dispatchSendings ()
{
    std::list<Conversation>::iterator current;

    while (!stop_sendThread) {
        listMx.lock();
        for (current = conversations.begin(); current != conversations.end(); ++current) {
            if (current->ttl == 0 && (current->response).length() == 0) {
                send(current->request);
                current->ttl = MAX_TTL;
            }
        }
        listMx.unlock();
    }
}

bool ComDispatcher::send (std::string message)
{
    bool response = true;
    int serialPortResponse = 0;

    serialPortResponse = port.send(message);
    if (serialPortResponse == 0 || serialPortResponse == 2) {
        response = false;
    }

    return response;
}

void ComDispatcher::dispatchReceipts ()
{
    while (!stop_receiveThread) {
        if (port.read() > 0) {
            printf("\n[INFO][ComDispatcher::dispatchReceipts] Message Received...");
            saveResponse(port.getDataRX());
        }
    }
}

bool ComDispatcher::saveResponse (std::string response)
{
    std::list<Conversation>::iterator current;
    bool result = false;

    listMx.lock();
    for (current = conversations.begin(); current != conversations.end(); ++current) {
        if (response.find(current->responseFormat) != std::string::npos) {
            printf("\n[INFO][ComDispatcher::saveReponse] Received message is a valid response for %s request.", current->request.c_str());
            current->response = response;
            (current->signal)->unlock();
            result = true;
            break;
        }
    }
    listMx.unlock();

    return result;
}

unsigned int ComDispatcher::getNewId ()
{
    conversationId++;

    return conversationId;
}

bool ComDispatcher::removeMessage (unsigned int id)
{
    std::list<Conversation>::iterator current;
    bool result = false;

    listMx.lock();
    for (current = conversations.begin(); current != conversations.end(); ++current) {
        if (current->id == id) {
            conversations.erase(current);
            result = true;
            break;
        }
    }
    listMx.unlock();

    return result;
}

bool ComDispatcher::removeResponseFromMessage (unsigned int id)
{
    std::list<Conversation>::iterator current;
    bool result = false;

    listMx.lock();
    for (current = conversations.begin(); current != conversations.end(); ++current) {
        if (current->id == id) {
            current->response = "";
            result = true;
            break;
        }
    }
    listMx.unlock();

    return result;
}

std::string ComDispatcher::getResponse (unsigned int id)
{
    Conversation* message;

    message = getMessage(id);

    return message->response;
}

Conversation* ComDispatcher::getMessage (unsigned int id)
{
    std::list<Conversation>::iterator current;
    Conversation* result = NULL;

    listMx.lock();
    for (current = conversations.begin(); current != conversations.end(); ++current) {
        if (current->id == id) {
            result = &(*current);
            break;
        }
    }
    listMx.unlock();

    return result;
}

void ComDispatcher::expireConversations ()
{
    std::list<Conversation>::iterator current;

    while (!stop_expireThread) {
        listMx.lock();
        for (current = conversations.begin(); current != conversations.end(); ++current) {
            if (current->ttl > 0 && (current->response).length() == 0) {
                (current->ttl)--;
            }
        }
        listMx.unlock();
        usleep(TTL_TIME);
    }
}
