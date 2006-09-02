/*******************************************************************************
 * Copyright (C) 2004-2006 Intel Corp. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of Intel Corp. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Intel Corp. OR THE CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

/**
 * @author Anas Nashif
 * @author Eugene Yarmosh
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <glib.h>

#include <libxml/uri.h>

#include "wsman-util.h"
#include "wsman-xml-api.h"
#include "wsman-soap.h"
#include "wsman-xml.h"

#include "wsman-dispatcher.h"
#include "wsman-xml-serializer.h"
#include "wsman-client.h"
#include "wsman-soap-envelope.h"
#include "wsman-debug.h"
#include "wsman-faults.h"
#include "wsman-soap-message.h"


WsXmlNsData g_wsNsData[] =
{
    {XML_NS_SOAP_1_2, "s"},
    {XML_NS_XML_NAMESPACES, "xml"}, // SUN stack requires the prefix to be xml
    {XML_NS_ADDRESSING, "wsa"},
    {XML_NS_DISCOVERY, "wsd"},
    {XML_NS_EVENTING, "wse"},
    {XML_NS_ENUMERATION, "wsen"},
    {XML_NS_SCHEMA_INSTANCE, "xsi"},
    {XML_NS_CIM_SCHEMA, "cim"},
    {XML_NS_WS_MAN_CAT, "cat"},
    {XML_NS_WSMAN_ID, "wsmid"},
    {XML_NS_XML_SCHEMA, "xs"},
    {XML_NS_WS_MAN, "wsman"},
    {NULL, NULL}
};

WsManDialectData g_wsDialectData[] =
{
    {WSM_WQL_FILTER_DIALECT, "wql"},
    {WSM_SELECTOR_FILTER_DIALECT, "selector"},
    {NULL, NULL}
};



SOAP_CALLBACK_ENTRY* make_soap_callback_entry(SoapServiceCallback proc, void* data, DL_List* listToAdd)
{
    SOAP_CALLBACK_ENTRY* entry = 
        (SOAP_CALLBACK_ENTRY*)soap_alloc(sizeof(SOAP_CALLBACK_ENTRY), 1);

    if ( entry )
    {
        entry->node.dataBuf = data;
        entry->proc = proc;
        if ( listToAdd )
            DL_AddTail(listToAdd, &entry->node);
    }

    return entry;
}

void ws_initialize_context(WsContextH hCntx, SoapH soap)
{
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    cntx->lastGetNameIndex = -1;
    cntx->bUserCreatedCtnx = 1;
    cntx->soap = soap;
}

WsContextH ws_create_context(SoapH soap)
{
    WS_CONTEXT* cntx = (WS_CONTEXT*)soap_alloc(sizeof(WS_CONTEXT), 1);
    if ( cntx )
        ws_initialize_context((WsContextH)cntx, soap);
    return (WsContextH)cntx;
}

SoapH ws_soap_initialize() 
{	
    SOAP_FW* fw = (SOAP_FW*)soap_alloc(sizeof(SOAP_FW), 1);
    if ( fw )
    {
        fw->dispatchList.listOwner = fw;
        fw->cntx = ws_create_context((SoapH)fw);    	
        soap_initialize_lock(fw);
    }	
    ws_xml_parser_initialize((SoapH)fw, g_wsNsData);
    soap_add_defalt_filter((SoapH)fw, outbound_addressing_filter, NULL, 0);
    soap_add_defalt_filter((SoapH)fw, outbound_control_header_filter, NULL, 0);
    return (SoapH)fw;
}



/**
 * Calculate needed space for interface array with Endpoints
 * @param interfaces List of interfaces
 * @return Needed size of WsManDispatcherInfo
 */
static int calculate_map_count(GList *interfaces)
{
    int count = 0;
    int j;

    GList *node = interfaces;
    while (node)
    {
        WsDispatchInterfaceInfo *interface = (WsDispatchInterfaceInfo *)node->data;
        for(j = 0; interface->endPoints[j].serviceEndPoint != NULL; j++)
            count++;
        node = g_list_next (node);	
    }

    return (g_list_length(interfaces) * sizeof(WsManDispatcherInfo)) 
        + ( count * sizeof(DispatchToEpMap) );
}

/**
 * Register Dispatcher
 * @param cntx Context
 * @param proc Dispatcher Callback
 * @param data Callback data
 */
static void ws_register_dispatcher(WsContextH cntx, DispatcherCallback proc, void* data)
{
    SOAP_FW* soap = (SOAP_FW*)ws_context_get_runtime(cntx);   
    if ( soap) {
        soap->dispatcherProc = proc;
        soap->dispatcherData = data;
    }
    return;
}


WsContextH ws_create_runtime (GList *interfaces)
{
    SoapH soap = ws_soap_initialize();
    if (soap && interfaces != NULL ) 
    {
        int size = calculate_map_count(interfaces);
        WsManDispatcherInfo* dispInfo = (WsManDispatcherInfo*)soap_alloc(size, 1);
        if ( dispInfo == NULL ) {
            soap_free(soap);
            soap = NULL;
        } else {     
            wsman_debug (WSMAN_DEBUG_LEVEL_MESSAGE, 
                    "Registering %d plugins", g_list_length(interfaces) );	
            dispInfo->interfaceCount = g_list_length(interfaces);
            dispInfo->interfaces = interfaces;
            GList* node = NULL;
            node = interfaces;        	
            while(node != NULL)
            {                    	
                if ( wsman_register_interface(((SOAP_FW*)soap)->cntx, 
                            ( WsDispatchInterfaceInfo*) node->data,
                            dispInfo) != 0 )
                {
                    wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, 
                            "Interface registeration failed");
                    soap_free(dispInfo);
                    soap = NULL;                   
                }                                               
                node = g_list_next (node);
            }                            
            if ( soap )
                ws_register_dispatcher(((SOAP_FW*)soap)->cntx, wsman_dispatcher, dispInfo);
        }		
    }
    return ((SOAP_FW*)soap)->cntx;
}




/**
 * Register Dispatcher Interfaces
 * @param cntx WS-Man Context
 * @param wsInterface Interface
 * @param dispinfo Dispatcher
 */
int wsman_register_interface(WsContextH cntx, WsDispatchInterfaceInfo* wsInterface,
        WsManDispatcherInfo* dispInfo)
{
    int retVal = 0;
    int i; 

    WsDispatchEndPointInfo* ep = wsInterface->endPoints;
    for(i = 0;  ep[i].serviceEndPoint != NULL; i++)
    {    	
        if ( (retVal = wsman_register_endpoint(cntx, 
                        wsInterface,                         
                        &ep[i],
                        dispInfo)) != 0 )
            break;
    }
    return retVal;
}



/*
 * Register Endpoint
 * @param cntx Context
 * @param wsInterface Interface
 * @param ep Endpoint
 * @param dispInfo Dispatcher information
 */
int wsman_register_endpoint(WsContextH cntx, WsDispatchInterfaceInfo* wsInterface,
        WsDispatchEndPointInfo* ep, WsManDispatcherInfo* dispInfo)
{
    wsman_debug (WSMAN_DEBUG_LEVEL_MESSAGE,"Registering Endpoint: %s, %s, %s, %s", 
            ep->inAction, ep->outAction, ep->respName, ep->rqstName);

    SoapDispatchH disp = NULL;
    unsigned long flags = SOAP_CUSTOM_DISPATCHER;
    SoapServiceCallback callbackProc = NULL;
    SoapH soap = ws_context_get_runtime(cntx);
    char* action = NULL;

    switch( (ep->flags & WS_DISP_TYPE_MASK) )
    {

    case WS_DISP_TYPE_IDENTIFY:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Identify");
        action = NULL;
        callbackProc = wsmid_identify_stub;
        flags |=  FLAG_IDENTIFY_REQUEST;
        break;
    case WS_DISP_TYPE_ENUMERATE:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Enumerate");
        action = ep->inAction;
        callbackProc = ws_enumerate_stub;
        break;
    case WS_DISP_TYPE_RELEASE:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Release");
        action = ep->inAction;
        callbackProc = ws_release_stub;
        break;
    case WS_DISP_TYPE_PULL:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Pull");
        action = ep->inAction;
        callbackProc = ws_pull_stub;
        break; 
    case WS_DISP_TYPE_PULL_RAW:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Pull Raw");
        action = ep->inAction;
        callbackProc = ws_pull_stub_raw;
        break;         
    case WS_DISP_TYPE_GET:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Get");
        action = ep->inAction;
        callbackProc = ws_transfer_get;      
        break;
    case WS_DISP_TYPE_GET_RAW:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Get Raw");
        action = ep->inAction;
        callbackProc = (SoapServiceCallback)ep->serviceEndPoint;   
        break;        
    case WS_DISP_TYPE_PUT_RAW:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Put Raw");
        action = ep->inAction;
        callbackProc = (SoapServiceCallback)ep->serviceEndPoint;   
        break;        
    case WS_DISP_TYPE_PUT:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for Put");
        action = ep->inAction;
        callbackProc = ws_transfer_put;
        break;

    case WS_DISP_TYPE_RAW_DOC:
        action = ep->inAction;
        callbackProc = (SoapServiceCallback)ep->serviceEndPoint;
        break;
        
    case WS_DISP_TYPE_CUSTOM_METHOD:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for custom method");
        action = ep->inAction;
        callbackProc = (SoapServiceCallback)ep->serviceEndPoint;   
        break;        

    case WS_DISP_TYPE_PRIVATE:
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG,"Registering endpoint for private EndPoint");
        action = ep->inAction;
        callbackProc = (SoapServiceCallback)ep->serviceEndPoint;   
        break;        

    default:
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR,
                "unknown dispatch type %i", ep->flags & WS_DISP_TYPE_MASK);
        break;
    }

    if ( callbackProc != NULL &&
            (disp = soap_create_dispatch(soap, action, NULL, NULL, callbackProc, ep, flags)) )
    {

        dispInfo->map[dispInfo->mapCount].ep = ep;	        
        dispInfo->map[dispInfo->mapCount].disp = disp;        
        dispInfo->mapCount++;

        soap_start_dispatch(disp);
    }

    if ( action && action != ep->inAction ) {
        soap_free(action);
    }

    return (disp == NULL);
}

int wsmid_identify_stub(SoapOpH op, void* appData) 
{
    wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Identify called");
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    SoapH soap = soap_get_op_soap(op);
    WsContextH cntx = ws_create_ep_context(soap, soap_get_op_doc(op, 1));    

    WsDispatchEndPointInfo* info = (WsDispatchEndPointInfo*)appData;
    XmlSerializerInfo* typeInfo = info->serializationInfo;
    WsEndPointGet endPoint = (WsEndPointGet)info->serviceEndPoint;

    void* data;
    WsXmlDocH doc = NULL;

    if ( (data = endPoint(cntx, status)) == NULL ) {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Transfer Get fault");
        doc = wsman_generate_fault(cntx, soap_get_op_doc(op, 1), WSMAN_FAULT_INVALID_SELECTORS, -1, NULL);
    } else {
        doc = ws_create_response_envelope(cntx, soap_get_op_doc(op, 1), NULL);
        ws_serialize(cntx, ws_xml_get_soap_body(doc), data, typeInfo, 
                NULL, (char*)info->data, (char*)info->data, 1); 
        ws_serializer_free_mem(cntx, data, typeInfo);
    }

    if ( doc ) {
        soap_set_op_doc(op, doc, 0);
    } else { 
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Response doc invalid");
    }

    ws_serializer_free_all(cntx);
    ws_destroy_context(cntx);
    soap_free(status);

    return 0;
}


int ws_transfer_put(SoapOpH op, void* appData)
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    SoapH soap = soap_get_op_soap(op);	
    WsContextH cntx = ws_create_ep_context(soap, soap_get_op_doc(op, 1));
    WsDispatchEndPointInfo* info = (WsDispatchEndPointInfo*)appData;
    XmlSerializerInfo* typeInfo = info->serializationInfo;
    WsEndPointPut endPoint = (WsEndPointPut)info->serviceEndPoint;

    void *data = ws_deserialize(cntx, 
            ws_xml_get_soap_body(soap_get_op_doc(op, 1)), 
            typeInfo, ws_xml_get_node_local_name(ws_xml_get_child(ws_xml_get_soap_body(soap_get_op_doc(op, 1)), 0 , NULL, NULL)),
            (char*)info->data, (char*)info->data, 0, 0); 

    int retVal = 0;
    WsXmlDocH doc = NULL;
    void* outData = NULL;

    if ( (retVal = endPoint(cntx, data, &outData, status)) )
    {
        doc = wsman_generate_fault(cntx, soap_get_op_doc(op, 1), WSMAN_FAULT_INVALID_SELECTORS, -1, NULL);
    } else {
        doc = ws_create_response_envelope(cntx, soap_get_op_doc(op, 1), NULL);
        if ( outData ) {
            ws_serialize(cntx, ws_xml_get_soap_body(doc), outData, 
                    typeInfo, NULL, (char*)info->data, (char*)info->data, 1); 
            ws_serializer_free_mem(cntx, outData, typeInfo);
        }
    }
    if ( doc ) {
        soap_set_op_doc(op, doc, 0);
    }
    ws_serializer_free_all(cntx);
    return retVal;
}




    


/**
 * Enumeration Stub for processing enumeration requests
 * @param op SOAP pperation handler
 * @param appData Application data
 * @return status
 */
int ws_enumerate_stub(SoapOpH op, void* appData)
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    SoapH soap = soap_get_op_soap(op);
    WsContextH soapCntx = ws_get_soap_context(soap);

    WsDispatchEndPointInfo* ep = (WsDispatchEndPointInfo*)appData;
    WsEndPointEnumerate endPoint = (WsEndPointEnumerate)ep->serviceEndPoint;
    WsEnumerateInfo enumInfo;
    WsXmlDocH doc = NULL;
    char cntxName[64] = WSFW_ENUM_PREFIX;
    char* enumId = &cntxName[sizeof(WSFW_ENUM_PREFIX) - 1];
    int retVal = 0;

    memset(&enumInfo, 0, sizeof(enumInfo));
    // TBD: ??? expires
    soap_get_uuid(enumId, sizeof(cntxName) - sizeof(WSFW_ENUM_PREFIX), 1);

    if ( endPoint && ( retVal =  
                endPoint(ws_create_ep_context(soap, soap_get_op_doc(op, 1)), &enumInfo, status)) ) {
        doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), status->fault_code, -1, NULL);
    } else {
        
        if ( enumInfo.pullResultPtr ) {
            doc = enumInfo.pullResultPtr;
            enumInfo.index++;
        } else {
            doc = ws_create_response_envelope(soapCntx, soap_get_op_doc(op, 1), NULL);
        }

        if ( doc )
        {
            WsXmlNodeH resp_node;
            wsman_set_estimated_total(soap_get_op_doc(op, 1), doc, &enumInfo);
            WsXmlNodeH body = ws_xml_get_soap_body(doc);
            if ( enumInfo.pullResultPtr == NULL) {
                resp_node = ws_xml_add_child(body, XML_NS_ENUMERATION, WSENUM_ENUMERATE_RESP, NULL);
            } else {
                resp_node = ws_xml_get_child(body, 0, XML_NS_ENUMERATION, WSENUM_ENUMERATE_RESP);
            }

            if (enumInfo.index == enumInfo.totalItems) {
                ws_serialize_str(soapCntx, resp_node, NULL, XML_NS_WS_MAN , WSENUM_END_OF_SEQUENCE);          
            } else {
                ws_serialize_str(soapCntx, resp_node, enumId, 
                        XML_NS_ENUMERATION, WSENUM_ENUMERATION_CONTEXT);
                ws_set_context_val(soapCntx, cntxName, &enumInfo, sizeof(enumInfo), 0);  
            }
        }
    }


    if ( doc ) {
        soap_set_op_doc(op, doc, 0);
    }
    soap_free(status);

    return retVal;
}

WsEnumerateInfo* get_enum_info(WsContextH cntx, WsXmlDocH doc, char* cntxName, int cntxNameLen,
        char* op, char** enumIdPtr)
{
    WsEnumerateInfo* enumInfo = NULL;
    char* enumId = NULL;
    WsXmlNodeH node = ws_xml_get_soap_body(doc);

    if ( node && (node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, op)) )
    {
        node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, WSENUM_ENUMERATION_CONTEXT);
        if ( node ) {
            enumId = ws_xml_get_node_text(node);
            if ( enumIdPtr != NULL ) {
                *enumIdPtr = enumId;
            }
        }
    }

    if ( enumId != NULL )
    {
        strcpy(cntxName, WSFW_ENUM_PREFIX);
        strncpy(&cntxName[sizeof(WSFW_ENUM_PREFIX) - 1],
                enumId,
                cntxNameLen - sizeof(WSFW_ENUM_PREFIX));
        enumInfo = (WsEnumerateInfo*)ws_get_context_val(cntx, cntxName, NULL);
    }
    return enumInfo;
}

int ws_release_stub(SoapOpH op, void* appData)
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    SoapH soap = soap_get_op_soap(op);
    WsContextH soapCntx = ws_get_soap_context(soap);
    WsDispatchEndPointInfo* ep = (WsDispatchEndPointInfo*)appData;
    WsEndPointRelease endPoint = (WsEndPointRelease)ep->serviceEndPoint;
    char cntxName[64];
    int retVal = 0;
    WsXmlDocH doc = NULL;
    WsEnumerateInfo* enumInfo = get_enum_info(soapCntx, soap_get_op_doc(op, 1), cntxName,
            sizeof(cntxName), WSENUM_RELEASE, NULL);

    if ( enumInfo == NULL ) {
        doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), WSEN_FAULT_INVALID_ENUMERATION_CONTEXT, -1, NULL);
    } else {
        if ( endPoint && (retVal = endPoint(soapCntx, enumInfo, status)) )
        {            
            wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "endPoint error");        		
            doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), 
                    WSMAN_FAULT_INTERNAL_ERROR, OWSMAN_FAULT_DETAIL_ENDPOINT_ERROR, NULL);   	            
        } else {
            doc = ws_create_response_envelope(soapCntx, soap_get_op_doc(op, 1), NULL);
            ws_remove_context_val(soapCntx, cntxName); 
        }
    }        
    if ( doc ) {
        soap_set_op_doc(op, doc, 0);
        ws_xml_destroy_doc(doc);
    }

    return retVal;
}

int ws_pull_stub(SoapOpH op, void* appData)
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    SoapH soap = soap_get_op_soap(op);
    WsContextH soapCntx = ws_get_soap_context(soap);

    //WsContextH soapCntx = ws_create_ep_context(soap, soap_get_op_doc(op, 1));

    WsDispatchEndPointInfo* ep = (WsDispatchEndPointInfo*)appData;
    XmlSerializerInfo* typeInfo = ep->serializationInfo;
    WsEndPointPull endPoint = (WsEndPointPull)ep->serviceEndPoint;
    char cntxName[64];
    int retVal = 0;
    WsXmlDocH doc = NULL;
    char* enumId = NULL;

    WsEnumerateInfo* enumInfo = get_enum_info(soapCntx, soap_get_op_doc(op, 1),
            cntxName, sizeof(cntxName), WSENUM_PULL, &enumId);

    if ( enumInfo == NULL ) {
        doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), 
                WSEN_FAULT_INVALID_ENUMERATION_CONTEXT, -1, NULL); 
    } else {
        if ( (retVal = endPoint(ws_create_ep_context(soap, soap_get_op_doc(op, 1)), enumInfo, status)) ) {             
            doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), WSEN_FAULT_INVALID_ENUMERATION_CONTEXT, -1, NULL);
            ws_remove_context_val(soapCntx, cntxName); 	
        } else {
            enumInfo->index++;
            if ( (doc = ws_create_response_envelope(soapCntx, soap_get_op_doc(op, 1), NULL)) )
            {
                wsman_set_estimated_total(soap_get_op_doc(op, 1), doc, enumInfo);
                WsXmlNodeH node = ws_xml_add_child(ws_xml_get_soap_body(doc), 
                        XML_NS_ENUMERATION, WSENUM_PULL_RESP, NULL);
                if ( node != NULL )
                {
                    if ( enumInfo->pullResultPtr )
                    {                       
                        WsXmlNodeH itemsNode = ws_xml_add_child(node, 
                                XML_NS_ENUMERATION, WSENUM_ITEMS, NULL);                            
                        ws_serialize(soapCntx, itemsNode, enumInfo->pullResultPtr, 
                                typeInfo, ep->respName, (char*)ep->data, (char*)ep->data, 1);                       
                        if ( enumId ) {
                            ws_serialize_str(soapCntx, node, enumId, 
                                    XML_NS_ENUMERATION, WSENUM_ENUMERATION_CONTEXT);
                        }
                        ws_serializer_free_mem(soapCntx, enumInfo->pullResultPtr, typeInfo);
                    } else {
                        ws_serialize_str(soapCntx, 
                                node, NULL, XML_NS_ENUMERATION, WSENUM_END_OF_SEQUENCE); 
                    }
                }
            }
        }
    }

    if ( doc )
    {
        soap_set_op_doc(op, doc, 0);
        ws_xml_destroy_doc(doc);
    }

    return retVal;
}

int ws_pull_stub_raw(SoapOpH op, void* appData)
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    WsXmlDocH doc = NULL;
    SoapH soap = soap_get_op_soap(op);
    WsContextH soapCntx = ws_get_soap_context(soap);
    WsDispatchEndPointInfo* ep = (WsDispatchEndPointInfo*)appData;

    WsEndPointPull endPoint = (WsEndPointPull)ep->serviceEndPoint;
    char cntxName[64];
    int retVal = 0;
    char* enumId = NULL;

    WsEnumerateInfo* enumInfo = get_enum_info(soapCntx, soap_get_op_doc(op, 1), cntxName,
            sizeof(cntxName), WSENUM_PULL, &enumId);

    if ( enumInfo == NULL ) {        
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Invalid enumeration context...");
        doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), WSEN_FAULT_INVALID_ENUMERATION_CONTEXT, -1, NULL);
    } else {
        if ( (retVal = endPoint(ws_create_ep_context(soap, soap_get_op_doc(op, 1)), enumInfo, status)) ) {             
            doc = wsman_generate_fault(soapCntx, soap_get_op_doc(op, 1), WSEN_FAULT_INVALID_ENUMERATION_CONTEXT, -1, NULL);
            ws_remove_context_val(soapCntx, cntxName); 	
        } else {
            enumInfo->index++;
            if ( enumInfo->pullResultPtr )
            {
                doc = 	enumInfo->pullResultPtr;
                wsman_set_estimated_total(soap_get_op_doc(op, 1), doc, enumInfo);
                WsXmlNodeH body =   ws_xml_get_soap_body(doc);
                WsXmlNodeH response = ws_xml_get_child(body, 0 , XML_NS_ENUMERATION,WSENUM_PULL_RESP);
                if (enumInfo->index == enumInfo->totalItems) {
                    ws_serialize_str(soapCntx, response, NULL, XML_NS_ENUMERATION, WSENUM_END_OF_SEQUENCE);          
                }
                else if ( enumId ) {
                    ws_serialize_str(soapCntx, response, enumId, 
                            XML_NS_ENUMERATION, WSENUM_ENUMERATION_CONTEXT);
                }
            }
        }
    }
    if ( doc ) {
        soap_set_op_doc(op, doc, 0);
    } else {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "doc is null");
    }    
    soap_free(status);
    return retVal;
}

int ws_transfer_get(SoapOpH op, void* appData )
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Transfer Get");
    SoapH soap = soap_get_op_soap(op);
    WsContextH cntx = ws_create_ep_context(soap, soap_get_op_doc(op, 1));    

    WsDispatchEndPointInfo* info = (WsDispatchEndPointInfo*)appData;
    XmlSerializerInfo* typeInfo = info->serializationInfo;
    WsEndPointGet endPoint = (WsEndPointGet)info->serviceEndPoint;

    void* data;
    WsXmlDocH doc = NULL;

    if ( (data = endPoint(cntx, status)) == NULL ) {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Transfer Get fault");
        doc = wsman_generate_fault(cntx, soap_get_op_doc(op, 1), WSMAN_FAULT_INVALID_SELECTORS, -1, NULL);
    } else {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Creating Response doc");
        doc = ws_create_response_envelope(cntx, soap_get_op_doc(op, 1), NULL);

        ws_serialize(cntx, ws_xml_get_soap_body(doc), data, typeInfo, 
                NULL, (char*)info->data, (char*)info->data, 1); 
        ws_serializer_free_mem(cntx, data, typeInfo);
    }

    if ( doc ) {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Setting operation document");
        soap_set_op_doc(op, doc, 0);
    } 
    else { 
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Response doc invalid");
    }

    ws_serializer_free_all(cntx);
    ws_destroy_context(cntx);
    soap_free(status);

    return 0;
}

/*
int ws_custom_method_stub(SoapOpH op, void* appData )
{
    WsmanStatus *status = soap_alloc(sizeof(WsmanStatus *), 0 );
    wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Custom Method");
    SoapH soap = soap_get_op_soap(op);
    WsContextH cntx = ws_create_ep_context(soap, soap_get_op_doc(op, 1));    

    WsDispatchEndPointInfo* info = (WsDispatchEndPointInfo*)appData;
    XmlSerializerInfo* typeInfo = info->serializationInfo;
    WsEndPointGet endPoint = (WsEndPointGet)info->serviceEndPoint;

    void* data;
    WsXmlDocH doc = NULL;

    if ( (data = endPoint(cntx, status)) == NULL ) {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Transfer Get fault");
        doc = wsman_generate_fault(cntx, soap_get_op_doc(op, 1), WSMAN_FAULT_INVALID_SELECTORS, -1);
    } else {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Creating Response doc");
        doc = ws_create_response_envelope(cntx, 
                soap_get_op_doc(op, 1), 
                NULL);

        ws_serialize(cntx, 
                ws_xml_get_soap_body(doc), 
                data, 
                typeInfo, 
                NULL,
                (char*)info->data, 
                (char*)info->data,
                1); 
        ws_serializer_free_mem(cntx, data, typeInfo);
    }

    if ( doc ) {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Setting operation document");
        soap_set_op_doc(op, doc, 0);
    } 
    else { 
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "Response doc invalid");
    }

    ws_serializer_free_all(cntx);
    ws_destroy_context(cntx);

    return 0;
}
*/



WsContextH ws_get_soap_context(SoapH soap)
{
    return ((SOAP_FW*)soap)->cntx;
}



void destroy_context_entry(WS_CONTEXT_ENTRY* entry)
{
    if ( (entry->options & WS_CONTEXT_FREE_DATA) != 0 )
        soap_free(entry->node.dataBuf);
    soap_free(entry->name);
    soap_free(entry);
}


void ws_clear_context_entries(WsContextH hCntx)
{
    if ( hCntx )
    {
        DL_List* list = &(((WS_CONTEXT*)hCntx)->entries);
        WS_CONTEXT_ENTRY* entry;
        while( (entry = (WS_CONTEXT_ENTRY*)DL_RemoveHead(list))  )
        {
            destroy_context_entry(entry);
        }
    }
}

int ws_remove_context_val(WsContextH hCntx, char* name)
{
    int retVal = 1;
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    if ( cntx && name )
    {
        WS_CONTEXT_ENTRY* entry;

        soap_fw_lock(cntx->soap);
        entry = (WS_CONTEXT_ENTRY*)DL_GetHead(&cntx->entries);
        if ( (entry = find_context_entry(entry, name, 0)) != NULL )
        {
            DL_RemoveNode(&entry->node);
            destroy_context_entry(entry);
            retVal = 0;
        }
        soap_fw_unlock(cntx->soap);
    }
    return retVal;
}

int set_context_val(WsContextH hCntx, 
        char* name, 
        void* val, 
        int size, 
        int bNoDup, 
        unsigned long type)
{
    wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Setting context value: %s", name);
    int retVal = 1;
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    if ( cntx && name )
    {
        void* ptr = val;
        unsigned long flags = type;

        if ( !bNoDup )
        {
            if ( val && (ptr = soap_alloc(size, 0)) )
            {
                memcpy(ptr, val, size);
            }
            flags |= WS_CONTEXT_FREE_DATA;
        }
        if ( ptr || val == NULL )
        {
            //soap_fw_lock(cntx->soap);
            ws_remove_context_val(hCntx, name);
            if ( create_context_entry(&cntx->entries, name, ptr, size, flags) )
            {
                retVal = 0;			
            }
            //soap_fw_unlock(cntx->soap);
        }
    } 
    else 
    {
        wsman_debug (WSMAN_DEBUG_LEVEL_ERROR, "error setting context value.");
    }
    return retVal;
}


int ws_set_context_val(WsContextH hCntx, char* name, void* val, int size, int bNoDup)
{
    int retVal = set_context_val(hCntx, name, val, size, bNoDup, WS_CONTEXT_TYPE_BLOB);
    return retVal;
}



int ws_set_context_ulong_val(WsContextH cntx, char* name, unsigned long val)
{
    int retVal = set_context_val(cntx, 
            name, 
            &val, 
            sizeof(unsigned long), 
            0, 
            WS_CONTEXT_TYPE_ULONG); 
    return retVal;
}



int ws_set_context_xml_doc_val(WsContextH cntx, char* name, WsXmlDocH val)
{
    int retVal = set_context_val(cntx, name, (void*)val, 0, 1, WS_CONTEXT_TYPE_XMLDOC); 
    return retVal;
}

WsContextH ws_create_ep_context(SoapH soap, WsXmlDocH doc)
{
    WsContextH cntx = ws_create_context(soap);
    if ( cntx ) 
        ws_set_context_xml_doc_val(cntx, WSFW_INDOC, doc);
    return cntx;
}


int ws_destroy_context(WsContextH hCntx)
{
    int retVal = 1;
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    if ( cntx && cntx->bUserCreatedCtnx ) {
        ws_clear_context_entries(hCntx);
        soap_free(cntx);
        retVal = 0;
    }
    return retVal;
}


WS_CONTEXT_ENTRY* create_context_entry(DL_List* list, 
        char* name, 
        void* val,
        unsigned long size,
        unsigned long flags)
{
    WS_CONTEXT_ENTRY* entry = (WS_CONTEXT_ENTRY*)soap_alloc(sizeof(WS_CONTEXT_ENTRY), 1);
    if ( entry )
    {
        entry->name = soap_clone_string(name);
        entry->options = flags;
        entry->node.dataBuf = val;
        if ( list )
        {
            WS_CONTEXT_ENTRY* next = (WS_CONTEXT_ENTRY*)DL_GetHead(list);
            while( next != NULL )
            {
                // TBD: segment by segment cmp to support indexes
                if ( strcmp(next->name, entry->name) > 0 )
                {
                    DL_AddBefore(&entry->node, &next->node);
                    break;
                }
                next = (WS_CONTEXT_ENTRY*)DL_GetNext(&entry->node);
            }		
            if ( next == NULL )	
                DL_AddTail(list, &entry->node);
        }
    }
    return entry;
}

SoapH ws_context_get_runtime(WsContextH hCntx)
{
    SoapH soap = NULL;
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    if ( cntx )
        soap = cntx->soap;
    return soap;
}

void* get_context_val(WsContextH hCntx, char* name, int* size, unsigned long type)
{
    char* val = NULL;
    WS_CONTEXT* cntx = (WS_CONTEXT*)hCntx;
    if ( cntx && name )
    {
        WS_CONTEXT_ENTRY* entry;
        soap_fw_lock(cntx->soap);
        entry = (WS_CONTEXT_ENTRY*)DL_GetHead(&cntx->entries);
        if ( (entry = find_context_entry(entry, name, 0)) != NULL )
        {
            if ( type == (entry->options & WS_CONTEXT_TYPE_MASK)
                    ||
                    type == WS_CONTEXT_TYPE_BLOB )
            {
                val = entry->node.dataBuf;
                if ( size )
                    *size = entry->size;
            }
        }
        soap_fw_unlock(cntx->soap);
    }
    return val;
}

WS_CONTEXT_ENTRY* find_context_entry(WS_CONTEXT_ENTRY* start, char* name, int bPrefix)
{
    WS_CONTEXT_ENTRY* entry = NULL;

    if ( start && name )
    {
        int len = strlen(name);
        entry = start;

        while(entry)
        {
            if ( (bPrefix && !strncmp(entry->name, name, len))
                    ||
                    (!bPrefix && !strcmp(entry->name, name)) )
            {
                break;
            }
            entry = (WS_CONTEXT_ENTRY*)DL_GetNext(&entry->node);
        }
    }
    return entry;
}


void* ws_get_context_val(WsContextH cntx, char* name, int* size)
{
    return get_context_val(cntx, name, size, WS_CONTEXT_TYPE_BLOB);
}


unsigned long ws_get_context_ulong_val(WsContextH cntx, char* name)
{
    void* ptr = get_context_val(cntx, name, NULL, WS_CONTEXT_TYPE_ULONG);
    if ( ptr != NULL )
        return *((unsigned long*)ptr);
    return 0;
}


SoapOpH soap_create_op(
        SoapH soap, 
        char* inboundAction, 
        char* outboundAction,// optional
        char* role, 
        SoapServiceCallback callbackProc, 
        void* callbackData,
        unsigned long flags, 
        unsigned long timeout)
{
    SOAP_DISPATCH_ENTRY* disp = NULL;
    SOAP_OP_ENTRY* entry = NULL;

    if ( (disp = (SOAP_DISPATCH_ENTRY*)soap_create_dispatch(soap, 
                    inboundAction, outboundAction, NULL, // reserved, must be NULL
                    callbackProc, callbackData, flags)) != NULL )
    {
        entry = create_op_entry((SOAP_FW*)soap, disp, NULL, timeout);
    }
    return (SoapOpH)entry;
}

int soap_add_disp_filter(
        SoapDispatchH disp,
        SoapServiceCallback callbackProc, 
        void* callbackData, 
        int inbound)
{
    SOAP_CALLBACK_ENTRY* entry = NULL;
    if ( disp )
    {
        DL_List* list = (!inbound) ? 
            &((SOAP_DISPATCH_ENTRY*)disp)->outboundFilterList : 
            &((SOAP_DISPATCH_ENTRY*)disp)->inboundFilterList;

        entry = make_soap_callback_entry(callbackProc, callbackData, list);
    }
    return (entry == NULL);
}


int soap_add_op_filter(SoapOpH op, 
        SoapServiceCallback proc, 
        void* data, 
        int inbound)
{
    if ( op )
        return soap_add_disp_filter((SoapDispatchH)((SOAP_OP_ENTRY*)op)->dispatch, 
                proc, 
                data,
                inbound);
    return 1;
}


/**
 * Get Operation Document
 * @param op Operation Handle
 * @param inbound Direction flag
 * @return XML Document
 */
WsXmlDocH soap_get_op_doc(SoapOpH op, int inbound)
{
    WsXmlDocH doc = NULL;
    if ( op )
    {
        SOAP_OP_ENTRY* e = (SOAP_OP_ENTRY*)op;
        doc = (!inbound) ? e->outDoc : e->inDoc;
    }        
    return doc;
}

WsXmlDocH soap_detach_op_doc(SoapOpH op, int inbound)
{
    WsXmlDocH doc = NULL;
    if ( op ) {
        SOAP_OP_ENTRY* e = (SOAP_OP_ENTRY*)op;
        if ( !inbound ) {
            doc = e->outDoc;
            e->outDoc = NULL;
        } else {
            doc = e->inDoc;
            e->inDoc = NULL;
        }
    }

    return doc;
}

int soap_set_op_doc(SoapOpH op, WsXmlDocH doc, int inbound)
{
    int retVal = 1;
    if ( op ) {
        SOAP_OP_ENTRY* e = (SOAP_OP_ENTRY*)op;
        if ( !inbound ) 
            e->outDoc = doc;
        else
            e->inDoc = doc;
        retVal = 0;
    }
    return retVal;
}

char* soap_get_op_action(SoapOpH op, int inbound)
{
    char* action = NULL;
    if ( op )
    {
        SOAP_OP_ENTRY* e = (SOAP_OP_ENTRY*)op;
        action = (!inbound) ? e->dispatch->outboundAction : 
            e->dispatch->inboundAction;
    }

    return action;
}

void soap_set_op_action(SoapOpH op, char* action, int inbound)
{
    if ( op && action )
    {
        SOAP_OP_ENTRY* e = (SOAP_OP_ENTRY*)op;

        if ( !inbound )
        {
            soap_free(e->dispatch->outboundAction);
            e->dispatch->outboundAction = soap_clone_string(action);
        }
        else
        {
            soap_free(e->dispatch->inboundAction);
            e->dispatch->inboundAction =  soap_clone_string(action);
        }
    }
}

unsigned long soap_get_op_flags(SoapOpH op)
{
    if ( op )
    {
        return ((SOAP_OP_ENTRY*)op)->dispatch->flags;
    }
    return 0;
}

SoapH soap_get_op_soap(SoapOpH op)
{
    if ( op )
        return (SoapH)((SOAP_OP_ENTRY*)op)->dispatch->fw;

    return NULL;
}

void soap_mark_processed_op_header(SoapOpH h, WsXmlNodeH xmlNode)
{
    SOAP_OP_ENTRY* op = (SOAP_OP_ENTRY*)h;
    if ( op && xmlNode )
    {
        DL_MakeNode(&op->processedHeaders, xmlNode);
    }
}

void soap_destroy_op(SoapOpH op)
{
    destroy_op_entry((SOAP_OP_ENTRY*)op);
}


SOAP_OP_ENTRY* create_op_entry(
        SOAP_FW* fw, 
        SOAP_DISPATCH_ENTRY* dispatch, 
        WsmanMessage  *data, 
        unsigned long timeout)
{
    SOAP_OP_ENTRY* entry = (SOAP_OP_ENTRY*)soap_alloc(sizeof(SOAP_OP_ENTRY), 1);
    if ( entry ) {
        entry->timeoutTicks = timeout;
        // entry->submittedTicks = SoapGetTicks();
        entry->dispatch = dispatch;
        entry->cntx = ws_create_context((SoapH)fw);       
        entry->data = data;
    }
    return entry;
}


void destroy_op_entry(SOAP_OP_ENTRY* entry)
{    
    if ( entry )
    {
        SOAP_FW* fw = entry->dispatch->fw;
        if ( fw )
            soap_fw_lock(fw);
        DL_RemoveNode(&entry->dispatch->node);
        unlink_response_entry(fw, entry);
        if ( fw ) {
            soap_fw_unlock(fw);
        }
        destroy_dispatch_entry(entry->dispatch);
        DL_RemoveAndDestroyAllNodes(&entry->processedHeaders, 0);
        // entry->inDoc and outDoc are not destroyed here
        ws_destroy_context(entry->cntx);
        soap_free(entry);
    }
}



void destroy_dispatch_entry(SOAP_DISPATCH_ENTRY* entry)
{
    if ( entry )
    {
        int usageCount;
        soap_fw_lock(entry->fw);
        entry->usageCount--;
        usageCount = entry->usageCount;
        if ( !usageCount )
            DL_RemoveNode(&entry->node);

        soap_fw_unlock(entry->fw);

        if ( !usageCount )
        {
            DL_RemoveAndDestroyAllNodes(&entry->inboundFilterList, 0);
            DL_RemoveAndDestroyAllNodes(&entry->outboundFilterList, 0);

            soap_free(entry->inboundAction);
            soap_free(entry->outboundAction);

            soap_free(entry);
        }
    }
}




WsXmlDocH ws_get_context_xml_doc_val(WsContextH cntx, char* name)
{
    return (WsXmlDocH)get_context_val(cntx, name, NULL, WS_CONTEXT_TYPE_XMLDOC);
}


void add_response_entry(SOAP_FW* fw, SOAP_OP_ENTRY* op)
{
    wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Adding Response Entry");
    soap_fw_lock(fw);

    DL_MakeNode(&fw->responseList, op);

    soap_fw_unlock(fw);
}

char *wsman_get_enum_mode(WsContextH cntx, WsXmlDocH doc) {
    char *enum_mode = NULL;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);

    if ( doc ) {
        WsXmlNodeH node = ws_xml_get_soap_body(doc);

        if ( node && (node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, WSENUM_ENUMERATE)) )
        {
            WsXmlNodeH opt = ws_xml_get_child(node, 0, XML_NS_WS_MAN, WSM_ENUM_MODE);
            if ( opt ) {
                char *text = ws_xml_get_node_text(opt);
                if (text != NULL)
                    enum_mode = text;
            }
        }
    } 
    return enum_mode;
}

void wsman_set_enum_mode(char *enum_mode, WsEnumerateInfo *enumInfo) 
{

    if (strcmp(enum_mode, WSM_ENUM_EPR) == 0 )
        enumInfo->flags |= FLAG_ENUMERATION_ENUM_EPR;
    else if (strcmp(enum_mode, WSM_ENUM_OBJ_AND_EPR) == 0 )
        enumInfo->flags |= FLAG_ENUMERATION_ENUM_OBJ_AND_EPR;

    return;
}


int wsman_is_optimization(WsContextH cntx, WsXmlDocH doc) {

    int max_elements = 0;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);

    if ( doc ) {
        WsXmlNodeH node = ws_xml_get_soap_body(doc);

        if ( node && (node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, WSENUM_ENUMERATE)) )
        {
            WsXmlNodeH opt = ws_xml_get_child(node, 0, XML_NS_WS_MAN, WSM_OPTIMIZE_ENUM);
            if ( opt ) {
                WsXmlNodeH max = ws_xml_get_child(node, 0, XML_NS_WS_MAN, WSM_MAX_ELEMENTS);
                if (max) {
                    char *text = ws_xml_get_node_text(max);
                    if (text != NULL)
                        max_elements = atoi(text);
                } else {
                    max_elements = 1;
                }
            }
        }
    } 
    return max_elements;
}

int wsen_get_max_elements(WsContextH cntx, WsXmlDocH doc) {

    int max_elements = 0;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);

    if ( doc ) {
        WsXmlNodeH node = ws_xml_get_soap_body(doc);

        if ( node && (node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, WSENUM_PULL)) )
        {
            node = ws_xml_get_child(node, 0, XML_NS_ENUMERATION, WSENUM_MAX_ELEMENTS);
            if ( node ) {
                char *text = ws_xml_get_node_text(node);
                if (text != NULL)
                    max_elements = atoi(text);
            }
        }
    } else {
        return 0;
    }
    return max_elements;

}

char* wsman_get_resource_uri(WsContextH cntx, WsXmlDocH doc)
{
    char* val = NULL;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);

    if ( doc )
    {
        WsXmlNodeH header = ws_xml_get_soap_header(doc);
        WsXmlNodeH node = ws_xml_get_child(header, 0, XML_NS_WS_MAN, WSM_RESOURCE_URI);
        val = (!node) ? NULL : ws_xml_get_node_text(node);
    }
    return val;
}


char* wsman_get_system_uri(WsContextH cntx, WsXmlDocH doc)
{
    WsXmlNodeH header = ws_xml_get_soap_header(doc);
    WsXmlNodeH node = ws_xml_get_child(header, 0, XML_NS_WS_MAN, WSM_SYSTEM);
    char* val = (!node) ? NULL : ws_xml_get_node_text(node);
    return val;
}



char *wsman_remove_query_string(char * resourceUri)
{
    char *result;
    xmlURIPtr uri;
    uri = xmlParseURI((const char *)resourceUri);
    uri->query = NULL;
    result =  (char *)xmlSaveUri(uri);
    if (uri)
        xmlFreeURI(uri);
    return result;

}

GList * wsman_get_selector_list(WsContextH cntx, WsXmlDocH doc)
{
    GList *keys = NULL;

    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);

    if ( doc )
    {
        WsXmlNodeH header = ws_xml_get_soap_header(doc);
        WsXmlNodeH node = ws_xml_get_child(header, 0, XML_NS_WS_MAN, WSM_SELECTOR_SET);
        if ( node )
        {
            WsXmlNodeH selector;
            int index = 0;

            while( (selector = ws_xml_get_child(node, index++, XML_NS_WS_MAN, WSM_SELECTOR)) )
            {
                char* attrVal = ws_xml_find_attr_value(selector, XML_NS_WS_MAN, WSM_NAME);
                if ( attrVal == NULL )
                    attrVal = ws_xml_find_attr_value(selector, NULL, WSM_NAME);

                wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Selector: %s", attrVal);
                if ( attrVal )
                {
                    WsSelectorInfo *sel= malloc(sizeof(WsSelectorInfo));
                    if (!sel) 
                        return NULL;
                    sel->key = attrVal;
                    sel->val = ws_xml_get_node_text(selector);
                    keys = g_list_append(keys, sel);
                }

            }
        }
    } else {
        wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "doc is null");
    }

    return keys;
}

char* wsman_get_selector(WsContextH cntx, WsXmlDocH doc, char* name, int index)
{
    char* val = NULL;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);
    if ( doc ) {
        //SoapH soap = WsContextGetRuntime(cntx);
        WsXmlNodeH header = ws_xml_get_soap_header(doc);
        WsXmlNodeH node = ws_xml_get_child(header, index, XML_NS_WS_MAN, WSM_SELECTOR_SET);

        if ( node )
        {
            WsXmlNodeH selector;
            int index = 0;

            while( (selector = ws_xml_get_child(node, index++, XML_NS_WS_MAN, WSM_SELECTOR)) )
            {
                char* attrVal = ws_xml_find_attr_value(selector, XML_NS_WS_MAN, WSM_NAME);
                /*WsXmlAttrH attr = WsXmlGetNodeAttr(selector, 0);
                  char* attrVal = WsXmlGetAttrValue(attr);*/
                if ( attrVal == NULL )
                    attrVal = ws_xml_find_attr_value(selector, NULL, WSM_NAME);

                if ( attrVal && !strcmp(attrVal, name) )
                {
                    val = ws_xml_get_node_text(selector);
                    break;
                }
            }
        }
    }
    wsman_debug( WSMAN_DEBUG_LEVEL_DEBUG, "Selector value for %s: %s", name, val );
    return val;
}

char* ws_addressing_get_action(WsContextH cntx, WsXmlDocH doc)
{
    char *val = NULL;
    if ( doc == NULL )
        doc = ws_get_context_xml_doc_val(cntx, WSFW_INDOC);
    if ( doc ) {
        WsXmlNodeH header = ws_xml_get_soap_header(doc);
        WsXmlNodeH node = ws_xml_get_child(header, 0, XML_NS_ADDRESSING, WSA_ACTION);
        val = (!node) ? NULL : ws_xml_get_node_text(node);
    }

    return val;
}

void  wsman_add_selector( WsXmlNodeH baseNode, char* name, char* val)
{
    WsXmlNodeH selector = NULL;
    WsXmlNodeH set = ws_xml_get_child(baseNode, 0, XML_NS_WS_MAN, WSM_SELECTOR_SET);

    if ( set || (set = ws_xml_add_child(baseNode, XML_NS_WS_MAN, WSM_SELECTOR_SET, NULL)) )
    {
        if ( (selector = ws_xml_add_child(set, XML_NS_WS_MAN, WSM_SELECTOR, val)) )
        {
            ws_xml_add_node_attr(selector, NULL,  WSM_NAME, name);
        }
    }
    return;
}



int soap_add_defalt_filter(SoapH soap, SoapServiceCallback callbackProc,
        void* callbackData, int inbound)
{
    SOAP_CALLBACK_ENTRY* entry = NULL;
    if ( soap ) {
        DL_List* list = (!inbound) ?
            &((SOAP_FW*)soap)->outboundFilterList :
            &((SOAP_FW*)soap)->inboundFilterList;

        entry = make_soap_callback_entry(callbackProc, callbackData, list);
    }
    return (entry == NULL);
}


int outbound_control_header_filter(SoapOpH opHandle, void* data)
{
    unsigned long size = 0;
    char *buf = NULL;
    int len, envelope_size;
    SOAP_FW* fw = (SOAP_FW*)soap_get_op_soap(opHandle);
    WsXmlDocH inDoc = soap_get_op_doc(opHandle, 1);
    WsXmlDocH outDoc = soap_get_op_doc(opHandle, 0);
    WsXmlNodeH inHeaders = get_soap_header_element(fw, inDoc, NULL, NULL);

    if ( inHeaders ) {
        if ( ws_xml_get_child(inHeaders, 0, XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE) != NULL )
        {
            size = ws_deserialize_uint32(NULL, inHeaders, 0, XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE);
            ws_xml_dump_memory_enc(outDoc, &buf, &len, "UTF-8");
            envelope_size = ws_xml_utf8_strlen(buf);
            if (envelope_size > size ) {
                wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Response message size exceeds what the client can handle, faulting: %d > %lu" ,
                        envelope_size, size);
                wsman_generate_encoding_fault((SOAP_OP_ENTRY*) opHandle, WSMAN_FAULT_DETAIL_MAX_ENVELOPE_SIZE_EXCEEDED);
            }
        }
    }
    return 0;
}

int outbound_addressing_filter(SoapOpH opHandle, void* data)
{
    SOAP_FW* fw = (SOAP_FW*)soap_get_op_soap(opHandle);
    WsXmlDocH inDoc = soap_get_op_doc(opHandle, 1);
    WsXmlDocH outDoc = soap_get_op_doc(opHandle, 0);
    WsXmlNodeH outHeaders = get_soap_header_element(fw, outDoc, NULL, NULL);

    if ( outHeaders )
    {
        if ( ws_xml_get_child(outHeaders, 0, XML_NS_ADDRESSING, WSA_MESSAGE_ID) == NULL )
        {
            char uuidBuf[100];
            soap_get_uuid(uuidBuf, sizeof(uuidBuf), 0);
            ws_xml_add_child(outHeaders, XML_NS_ADDRESSING, WSA_MESSAGE_ID, uuidBuf);
            wsman_debug (WSMAN_DEBUG_LEVEL_DEBUG, "Adding message id: %s" , uuidBuf);
        }

        if ( inDoc != NULL )
        {
            WsXmlNodeH inMsgIdNode;
            if ( (inMsgIdNode = get_soap_header_element(fw, inDoc, XML_NS_ADDRESSING,
                            WSA_MESSAGE_ID)) != NULL && 
                    !ws_xml_get_child(outHeaders, 0, XML_NS_ADDRESSING, WSA_RELATES_TO) )
            {
                ws_xml_add_child(outHeaders, XML_NS_ADDRESSING, WSA_RELATES_TO, ws_xml_get_node_text(inMsgIdNode));
            }
        }
    }
    return 0;
}





int soap_xml_wait_for_response(SoapOpH op, unsigned long tm)
{
    int retVal = -1;

    if ( op )
    {
        unsigned long startTicks = soap_get_ticks();

        while( !(retVal = (soap_get_op_doc(op, 1) != NULL)) )
        {
            if ( is_time_up(startTicks, tm) )
                break;
            soap_sleep(50);
        }
    }    
    return retVal;
}


void soap_enter(SoapH soap)
{
    soap_fw_lock((SOAP_FW*)soap);
}

void soap_leave(SoapH soap)
{
    soap_fw_unlock((SOAP_FW*)soap);
}


void soap_destroy_fw(SoapH soap)
{
    SOAP_FW* fw = (SOAP_FW*)soap;

    if ( fw->dispatcherProc )
        fw->dispatcherProc(fw->cntx, fw->dispatcherData, NULL);

    /*
    while( !DL_IsEmpty(&fw->channelList) )
    {
        DL_Node* node = DL_RemoveHead(&fw->channelList);
        destroy_soap_channel((SOAP_CHANNEL*)node, SOAP_SEND_CALLBACK_REASON_SHUTDOWN);
    }
    */

    /*
    while( !DL_IsEmpty(&fw->sendWaitingList) )
    {
        DL_Node* node = DL_RemoveHead(&fw->channelList);
        destroy_output_chain((SOAP_OUTPUT_CHAIN*)node,
                SOAP_SEND_CALLBACK_REASON_SHUTDOWN);
    }
    */

    while( !DL_IsEmpty(&fw->dispatchList) )
    {
        destroy_dispatch_entry((SOAP_DISPATCH_ENTRY*)DL_GetHead(&fw->dispatchList));
    }

    while( !DL_IsEmpty(&fw->processedMsgIdList) )
    {
        DL_Node* node = DL_RemoveHead(&fw->processedMsgIdList);
        soap_free(node->dataBuf);
        soap_free(node);
    }
    while( !DL_IsEmpty(&fw->responseList) )
    {
        DL_Node* node = DL_RemoveHead(&fw->responseList);
        SOAP_OP_ENTRY* entry = (SOAP_OP_ENTRY*)node->dataBuf;
        destroy_op_entry(entry);
        soap_free(node);
    }

    DL_RemoveAndDestroyAllNodes(&fw->inboundFilterList, 0);
    DL_RemoveAndDestroyAllNodes(&fw->outboundFilterList, 0);

    ws_xml_parser_destroy((SoapH)soap);
    //soap_free(fw->addrWakeUp);
    ws_destroy_context(fw->cntx);
    //soap_destroy_lock(fw);
    soap_free(soap);
    return;
}

void wsman_set_estimated_total(WsXmlDocH in_doc, 
        WsXmlDocH out_doc, 
        WsEnumerateInfo *enumInfo) 
{
    WsXmlNodeH header = ws_xml_get_soap_header( in_doc);
    if (ws_xml_get_child(header,0 , 
                XML_NS_WS_MAN, 
                WSM_REQUEST_TOTAL) != NULL) {
        if (out_doc) {
            WsXmlNodeH response_header = ws_xml_get_soap_header( out_doc);
            if (enumInfo->totalItems >= 0 )
                ws_xml_add_child_format(response_header, 
                        XML_NS_WS_MAN, 
                        WSM_TOTAL_ESTIMATE, "%d", enumInfo->totalItems);
        }
    }
    return;
}

int wsman_is_identify_request(WsXmlDocH doc) {

    WsXmlNodeH node = ws_xml_get_soap_body(doc);
    node = ws_xml_get_child(node, 0, XML_NS_WSMAN_ID, WSMID_IDENTIFY);
    if (node)
        return 1;
    else
        return 0;
}

