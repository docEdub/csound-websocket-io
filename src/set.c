#include "set.h"

static void sendWebsocketPathData(CSOUND *csound, struct lws *websocket, WebsocketPathData *pathData)
{
    while (true) {
        int messageIndex = -1;
        const int read = csound->ReadCircularBuffer(csound, pathData->messageIndexCircularBuffer, &messageIndex, 1);
        if (read == 1) {
            // Make sure we're reading the most recent message to send to the websocket.
            int unused = -1;
            if (csound->PeekCircularBuffer(csound, pathData->messageIndexCircularBuffer, &unused, 1)) {
                continue;
            }

            WebsocketMessage *msg = pathData->messages + messageIndex;
            lws_write(websocket, (unsigned char*) msg->buffer, msg->size, LWS_WRITE_BINARY);
        }
        else {
            break;
        }
    }
}

static void sendWebsocketPathHashTable(CSOUND *csound, struct lws *websocket, CS_HASH_TABLE *pathHashTable)
{
    CONS_CELL *pathItem = csound->GetHashTableValues(csound, pathHashTable);
    while (pathItem) {
        WebsocketPathData *pathData = pathItem->value;
        sendWebsocketPathData(csound, websocket, pathData);
        pathItem = pathItem->next;
    }
}

int32_t onWebsocketWritable(struct lws *websocket)
{
    const struct lws_protocols *protocol = lws_get_protocol(websocket);
    Websocket *ws = protocol->user;
    CSOUND *csound = ws->csound;

    sendWebsocketPathHashTable(csound, websocket, ws->pathSetFloatsHashTable);
    sendWebsocketPathHashTable(csound, websocket, ws->pathSetStringHashTable);

    return OK;
}

int32_t websocket_set_destroy(CSOUND *csound, void *vp)
{
    WS_set *p = vp;
    releaseWebsocket(csound, p->common.websocket);
    return OK;
}

int32_t websocket_set_init(CSOUND *csound, WS_set *p)
{
    initPlugin();

    p->common.csound = csound;

    initPortKey(&p->common.portKey, *p->port);
    p->common.websocket = getWebsocket(csound, *p->port, &p->common);

    csound->RegisterDeinitCallback(csound, p, websocket_set_destroy);

    size_t pathLength = strlen(p->path->data);

    const CS_TYPE *type = csound->GetTypeForArg(p->input);
    const char *typeName = type->varTypeName;
    const uint8_t dataType = ('S' == typeName[0]) ? StringType : Float64ArrayType;

    p->msgPreSize = pathLength + 2; // Path length + null terminator + data type.
    p->msgPre = csound->Calloc(csound, p->msgPreSize);

    char *d = p->msgPre;
    memcpy(d, p->path->data, strlen(p->path->data));
    d += pathLength + 1; // Advance past path's null terminator.

    *d = dataType;

    return OK;
}

static void writeWebsocketPathDataMessage(CSOUND *csound, CS_HASH_TABLE *pathHashTable, WS_set *p, int dataType, const void *data, size_t dataSize)
{
    WebsocketPathData *pathData = getWebsocketPathData(csound, pathHashTable, p->path->data);

    const size_t msgSize = p->msgPreSize + dataSize + ((Float64ArrayType == dataType) ? 4 : 0);

    WebsocketMessage *msg = pathData->messages + pathData->messageIndex;
    if (msg->size < msgSize) {
        csound->Free(csound, msg->buffer);
        msg->buffer = csound->Malloc(csound, 2 * msgSize);
        msg->size = msgSize;
    }

    char *d = msg->buffer;
    memcpy(d, p->msgPre, p->msgPreSize); // Path and data type.
    d += p->msgPreSize;

    if (Float64ArrayType == dataType) {
        *(uint32_t*)(d) = dataSize / sizeof(MYFLT); // Array length.
        d += 4;
    }

    memcpy(d, data, dataSize); // Array or String data.

    writeWebsocketPathDataMessageIndex(csound, pathData);
}

int32_t websocket_setArray_perf(CSOUND *csound, WS_set *p) {
    ARRAYDAT *input = p->input;
    writeWebsocketPathDataMessage(csound, p->common.websocket->pathSetFloatsHashTable, p, Float64ArrayType, input->data, input->allocated);
    return OK;
}

int32_t websocket_setString_perf(CSOUND *csound, WS_set *p) {
    STRINGDAT *input = p->input;
    writeWebsocketPathDataMessage(csound, p->common.websocket->pathSetStringHashTable, p, StringType, input->data, input->size);
    return OK;
}
