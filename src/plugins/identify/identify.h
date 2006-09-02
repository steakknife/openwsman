//
// wsmid_identify_data.h
//
// This file is intended to be eventually generated by a codegen tool
//
#ifndef __IDENTIFY_H__
#define __IDENNTIFY_H__


// The resource is modeled as a struct
struct __wsmid_identify
{
	char* ProtocolVersion;
	char* ProductVendor;
	char* ProductVersion;
};
typedef struct __wsmid_identify wsmid_identify;

// Service endpoint declaration
wsmid_identify* wsmid_identify_Identify_EP (WsContextH cntx);


// Two macro below will declare 
// Serialization type info declaration for the resource
// extern struct __XmlSerializerInfo wsmid_identify_TypeInfo[];
// End point array declaration for the resource
// extern WsDispatchEndPointInfo wsmid_identify_EndPoints[];
SER_DECLARE_TYPE(wsmid_identify);
SER_DECLARE_EP_ARRAY(wsmid_identify);

void get_endpoints(GModule *self, void **data);
int init (GModule *self, void **data );
void cleanup( GModule *self, void *data );


#endif 