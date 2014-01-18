/* ip2locationStub.h
   Generated by gSOAP 2.7.9l from ip2locationMS_wsdl.h
   Copyright(C) 2000-2007, Robert van Engelen, Genivia Inc. All Rights Reserved.
   This part of the software is released under one of the following licenses:
   GPL, the gSOAP public license, or Genivia's license for commercial use.
*/

#ifndef ip2locationStub_H
#define ip2locationStub_H
#ifndef WITH_NONAMESPACES
#define WITH_NONAMESPACES
#endif
#include "stdsoap2.h"
#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
 *                                                                            *
 * Enumerations                                                               *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Classes and Structs                                                        *
 *                                                                            *
\******************************************************************************/


#ifndef SOAP_TYPE_ns1__IP2LOCATION
#define SOAP_TYPE_ns1__IP2LOCATION (6)
/* ns1:IP2LOCATION */
struct ns1__IP2LOCATION
{
	char *COUNTRYCODE;	/* optional element of type xsd:string */
	char *COUNTRYNAME;	/* optional element of type xsd:string */
	char *REGION;	/* optional element of type xsd:string */
	char *CITY;	/* optional element of type xsd:string */
	float LATITUDE;	/* required element of type xsd:float */
	float LONGITUDE;	/* required element of type xsd:float */
	char *ZIPCODE;	/* optional element of type xsd:string */
	char *ISPNAME;	/* optional element of type xsd:string */
	char *DOMAINNAME;	/* optional element of type xsd:string */
	char *CREDITSAVAILABLE;	/* optional element of type xsd:string */
	char *MESSAGE;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE__ns1__IP2Location
#define SOAP_TYPE__ns1__IP2Location (8)
/* ns1:IP2Location */
struct _ns1__IP2Location
{
	char *IP;	/* optional element of type xsd:string */
	char *LICENSE;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE__ns1__IP2LocationResponse
#define SOAP_TYPE__ns1__IP2LocationResponse (9)
/* ns1:IP2LocationResponse */
struct _ns1__IP2LocationResponse
{
	struct ns1__IP2LOCATION *IP2LocationResult;	/* SOAP 1.2 RPC return element (when namespace qualified) */	/* optional element of type ns1:IP2LOCATION */
};
#endif

#ifndef SOAP_TYPE___ns1__IP2Location
#define SOAP_TYPE___ns1__IP2Location (14)
/* Operation wrapper: */
struct __ns1__IP2Location
{
	struct _ns1__IP2Location *ns1__IP2Location;	/* optional element of type ns1:IP2Location */
};
#endif

#ifndef SOAP_TYPE_SOAP_ENV__Header
#define SOAP_TYPE_SOAP_ENV__Header (15)
/* SOAP Header: */
struct SOAP_ENV__Header
{
#ifdef WITH_NOEMPTYSTRUCT
	char dummy;	/* dummy member to enable compilation */
#endif
};
#endif

#ifndef SOAP_TYPE_SOAP_ENV__Code
#define SOAP_TYPE_SOAP_ENV__Code (16)
/* SOAP Fault Code: */
struct SOAP_ENV__Code
{
	char *SOAP_ENV__Value;	/* optional element of type xsd:QName */
	struct SOAP_ENV__Code *SOAP_ENV__Subcode;	/* optional element of type SOAP-ENV:Code */
};
#endif

#ifndef SOAP_TYPE_SOAP_ENV__Detail
#define SOAP_TYPE_SOAP_ENV__Detail (18)
/* SOAP-ENV:Detail */
struct SOAP_ENV__Detail
{
	int __type;	/* any type of element <fault> (defined below) */
	void *fault;	/* transient */
	char *__any;
};
#endif

#ifndef SOAP_TYPE_SOAP_ENV__Reason
#define SOAP_TYPE_SOAP_ENV__Reason (21)
/* SOAP-ENV:Reason */
struct SOAP_ENV__Reason
{
	char *SOAP_ENV__Text;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE_SOAP_ENV__Fault
#define SOAP_TYPE_SOAP_ENV__Fault (22)
/* SOAP Fault: */
struct SOAP_ENV__Fault
{
	char *faultcode;	/* optional element of type xsd:QName */
	char *faultstring;	/* optional element of type xsd:string */
	char *faultactor;	/* optional element of type xsd:string */
	struct SOAP_ENV__Detail *detail;	/* optional element of type SOAP-ENV:Detail */
	struct SOAP_ENV__Code *SOAP_ENV__Code;	/* optional element of type SOAP-ENV:Code */
	struct SOAP_ENV__Reason *SOAP_ENV__Reason;	/* optional element of type SOAP-ENV:Reason */
	char *SOAP_ENV__Node;	/* optional element of type xsd:string */
	char *SOAP_ENV__Role;	/* optional element of type xsd:string */
	struct SOAP_ENV__Detail *SOAP_ENV__Detail;	/* optional element of type SOAP-ENV:Detail */
};
#endif

/******************************************************************************\
 *                                                                            *
 * Types with Custom Serializers                                              *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Typedefs                                                                   *
 *                                                                            *
\******************************************************************************/

#ifndef SOAP_TYPE__XML
#define SOAP_TYPE__XML (4)
typedef char *_XML;
#endif

#ifndef SOAP_TYPE__QName
#define SOAP_TYPE__QName (5)
typedef char *_QName;
#endif


/******************************************************************************\
 *                                                                            *
 * Typedef Synonyms                                                           *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Externals                                                                  *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Stubs                                                                      *
 *                                                                            *
\******************************************************************************/


SOAP_FMAC5 int SOAP_FMAC6 soap_call___ns1__IP2Location(struct soap *soap, const char *soap_endpoint, const char *soap_action, struct _ns1__IP2Location *ns1__IP2Location, struct _ns1__IP2LocationResponse *ns1__IP2LocationResponse);

#ifdef __cplusplus
}
#endif

#endif

/* End of ip2locationStub.h */
