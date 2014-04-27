/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

/*
#include <srs_app_edge.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

class SrsSocket;
class SrsRtmpServer;
class SrsSource;
class SrsRequest;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsRtmpClient;
class SrsCommonMessage;
class ISrsProtocolReaderWriter;

/**
* the state of edge, auto machine
*/
enum SrsEdgeState
{
    SrsEdgeStateInit = 0,

    // for play edge
    SrsEdgeStatePlay = 100,
    // play stream from origin, ingest stream
    SrsEdgeStateIngestConnected,
    
    // for publish edge
    SrsEdgeStatePublish = 200,
};

/**
* the state of edge from user, manual machine
*/
enum SrsEdgeUserState
{
    SrsEdgeUserStateInit = 0,
    SrsEdgeUserStateReloading = 100,
};

/**
* edge used to ingest stream from origin.
*/
class SrsEdgeIngester : public ISrsThreadHandler
{
private:
    int stream_id;
private:
    SrsSource* _source;
    SrsPlayEdge* _edge;
    SrsRequest* _req;
    SrsThread* pthread;
    st_netfd_t stfd;
    ISrsProtocolReaderWriter* io;
    SrsRtmpClient* client;
    int origin_index;
public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();
public:
    virtual int initialize(SrsSource* source, SrsPlayEdge* edge, SrsRequest* req);
    virtual int start();
    virtual void stop();
// interface ISrsThreadHandler
public:
    virtual int cycle();
private:
    virtual int ingest();
    virtual void close_underlayer_socket();
    virtual int connect_server();
    virtual int process_publish_message(SrsCommonMessage* msg);
};

class SrsEdgeProxyContext
{
public:
    int edge_stream_id;
    st_netfd_t edge_stfd;
    ISrsProtocolReaderWriter* edge_io;
    SrsRtmpServer* edge_rtmp;
public:
    int origin_stream_id;
    st_netfd_t origin_stfd;
    ISrsProtocolReaderWriter* origin_io;
    SrsRtmpClient* origin_rtmp;
public:
    SrsEdgeProxyContext();
    virtual ~SrsEdgeProxyContext();
};

/**
* edge used to forward stream to origin.
*/
class SrsEdgeForwarder
{
private:
    int stream_id;
private:
    SrsSource* _source;
    SrsPublishEdge* _edge;
    SrsRequest* _req;
    st_netfd_t stfd;
    ISrsProtocolReaderWriter* io;
    SrsRtmpClient* client;
    int origin_index;
public:
    SrsEdgeForwarder();
    virtual ~SrsEdgeForwarder();
public:
    virtual int initialize(SrsSource* source, SrsPublishEdge* edge, SrsRequest* req);
    virtual int start();
    virtual void stop();
public:
    virtual int proxy(SrsEdgeProxyContext* context);
private:
    virtual int proxy_origin_message(SrsEdgeProxyContext* context);
    virtual int proxy_edge_message(SrsEdgeProxyContext* context);
    virtual void close_underlayer_socket();
    virtual int connect_server();
};

/**
* play edge control service.
* downloading edge speed-up.
*/
class SrsPlayEdge
{
private:
    SrsEdgeState state;
    SrsEdgeUserState user_state;
    SrsEdgeIngester* ingester;
public:
    SrsPlayEdge();
    virtual ~SrsPlayEdge();
public:
    virtual int initialize(SrsSource* source, SrsRequest* req);
    /**
    * when client play stream on edge.
    */
    virtual int on_client_play();
    /**
    * when all client stopped play, disconnect to origin.
    */
    virtual void on_all_client_stop();
public:
    /**
    * when ingester start to play stream.
    */
    virtual int on_ingest_play();
};

/**
* publish edge control service.
* uploading edge speed-up.
*/
class SrsPublishEdge
{
private:
    SrsEdgeState state;
    SrsEdgeUserState user_state;
    SrsEdgeForwarder* forwarder;
public:
    SrsPublishEdge();
    virtual ~SrsPublishEdge();
public:
    virtual int initialize(SrsSource* source, SrsRequest* req);
    /**
    * when client publish stream on edge.
    */
    virtual int on_client_publish();
    /**
    * proxy publish stream to edge
    */
    virtual int on_proxy_publish(SrsEdgeProxyContext* context);
};

#endif
