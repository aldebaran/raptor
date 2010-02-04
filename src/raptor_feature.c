/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * raptor_feature.c - Parser and Serializer features
 *
 * Copyright (C) 2004-2010, David Beckett http://www.dajobe.org/
 * Copyright (C) 2004-2005, University of Bristol, UK http://www.bristol.ac.uk/
 * 
 * This package is Free Software and part of Redland http://librdf.org/
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 * 
 */


#ifdef HAVE_CONFIG_H
#include <raptor_config.h>
#endif

#ifdef WIN32
#include <win32_raptor_config.h>
#endif


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* Raptor includes */
#include "raptor.h"
#include "raptor_internal.h"


#define AREA_PARSER (1)
#define AREA_SERIALIZER (2)
#define AREA_XML_WRITER (8)

#define VT_BOOL (0)
#define VT_STRING (4)
#define VT_INT (16)
#define VT_URI (4+32)

static const struct
{
  raptor_feature feature;
  /* flag bits
   *  0: default is bool/int value
   *
   *  1=parser feature
   *  2=serializer feature
   *  4=string/uri value (else bool/int)
   *  8=xml writer feature
   * 16=bool/int value is an integer
   * 32=string/uri value is a uri
   */
  unsigned int flags;
  const char *name;
  const char *label;
} raptor_features_list[RAPTOR_FEATURE_LAST+1] = {
  { RAPTOR_FEATURE_SCANNING                , AREA_PARSER | VT_BOOL, "scanForRDF", "Scan for rdf:RDF in XML content" },
  { RAPTOR_FEATURE_ASSUME_IS_RDF           , AREA_PARSER | VT_BOOL, "assumeIsRDF", "Assume content is RDF/XML, don't require rdf:RDF" },
  { RAPTOR_FEATURE_ALLOW_NON_NS_ATTRIBUTES , AREA_PARSER | VT_BOOL, "allowNonNsAttributes", "Allow bare 'name' rather than namespaced 'rdf:name'" },
  { RAPTOR_FEATURE_ALLOW_OTHER_PARSETYPES  , AREA_PARSER | VT_BOOL, "allowOtherParsetypes", "Allow user-defined rdf:parseType values" },
  { RAPTOR_FEATURE_ALLOW_BAGID             , AREA_PARSER | VT_BOOL, "allowBagID", "Allow rdf:bagID" },
  { RAPTOR_FEATURE_ALLOW_RDF_TYPE_RDF_LIST , AREA_PARSER | VT_BOOL, "allowRDFtypeRDFlist", "Generate the collection rdf:type rdf:List triple" },
  { RAPTOR_FEATURE_NORMALIZE_LANGUAGE      , AREA_PARSER | VT_BOOL, "normalizeLanguage", "Normalize xml:lang values to lowercase" },
  { RAPTOR_FEATURE_NON_NFC_FATAL           , AREA_PARSER | VT_BOOL, "nonNFCfatal", "Make non-NFC literals cause a fatal error" },
  { RAPTOR_FEATURE_WARN_OTHER_PARSETYPES   , AREA_PARSER | VT_BOOL, "warnOtherParseTypes", "Warn about unknown rdf:parseType values" },
  { RAPTOR_FEATURE_CHECK_RDF_ID            , AREA_PARSER | VT_BOOL, "checkRdfID", "Check rdf:ID values for duplicates" },
  { RAPTOR_FEATURE_RELATIVE_URIS           , AREA_SERIALIZER | VT_BOOL, "relativeURIs", "Write relative URIs wherever possible in serializing." },
  { RAPTOR_FEATURE_START_URI               , AREA_SERIALIZER | VT_URI,  "startURI", "Start URI for serializing to use." },
  { RAPTOR_FEATURE_WRITER_AUTO_INDENT      , AREA_XML_WRITER | VT_BOOL, "autoIndent", "Automatically indent elements." },
  { RAPTOR_FEATURE_WRITER_AUTO_EMPTY       , AREA_XML_WRITER | VT_BOOL, "autoEmpty", "Automatically detect and abbreviate empty elements." },
  { RAPTOR_FEATURE_WRITER_INDENT_WIDTH     , AREA_XML_WRITER | VT_BOOL, "indentWidth", "Number of spaces to indent." },
  { RAPTOR_FEATURE_WRITER_XML_VERSION      , AREA_SERIALIZER | AREA_XML_WRITER | VT_INT, "xmlVersion", "XML version to write." },
  { RAPTOR_FEATURE_WRITER_XML_DECLARATION  , AREA_SERIALIZER | AREA_XML_WRITER | VT_BOOL, "xmlDeclaration", "Write XML declaration." },
  { RAPTOR_FEATURE_NO_NET                  , AREA_PARSER | VT_BOOL,  "noNet", "Deny network requests." },
  { RAPTOR_FEATURE_RESOURCE_BORDER   , AREA_SERIALIZER | VT_STRING, "resourceBorder", "DOT serializer resource border color" },
  { RAPTOR_FEATURE_LITERAL_BORDER    , AREA_SERIALIZER | VT_STRING, "literalBorder", "DOT serializer literal border color" },
  { RAPTOR_FEATURE_BNODE_BORDER      , AREA_SERIALIZER | VT_STRING, "bnodeBorder", "DOT serializer blank node border color" },
  { RAPTOR_FEATURE_RESOURCE_FILL     , AREA_SERIALIZER | VT_STRING, "resourceFill", "DOT serializer resource fill color" },
  { RAPTOR_FEATURE_LITERAL_FILL      , AREA_SERIALIZER | VT_STRING, "literalFill", "DOT serializer literal fill color" },
  { RAPTOR_FEATURE_BNODE_FILL        , AREA_SERIALIZER | VT_STRING, "bnodeFill", "DOT serializer blank node fill color" },
  { RAPTOR_FEATURE_HTML_TAG_SOUP     , AREA_PARSER | VT_BOOL,  "htmlTagSoup", "HTML parsing uses a lax HTML parser" },
  { RAPTOR_FEATURE_MICROFORMATS      , AREA_PARSER | VT_BOOL,  "microformats", "GRDDL parsing looks for microformats" },
  { RAPTOR_FEATURE_HTML_LINK         , AREA_PARSER | VT_BOOL,  "htmlLink", "GRDDL parsing looks for <link type=\"application/rdf+xml\">" },
  { RAPTOR_FEATURE_WWW_TIMEOUT       , AREA_PARSER | VT_INT,  "wwwTimeout", "Set internal WWW URI retrieval timeout" },
  { RAPTOR_FEATURE_WRITE_BASE_URI    , AREA_SERIALIZER | VT_BOOL,  "writeBaseURI", "Write @base / xml:base directive in serializer output" },
  { RAPTOR_FEATURE_WWW_HTTP_CACHE_CONTROL, AREA_PARSER | VT_STRING, "wwwHttpCacheControl", "Set HTTP Cache-Control: header value" },
  { RAPTOR_FEATURE_WWW_HTTP_USER_AGENT , AREA_PARSER | VT_STRING, "wwwHttpUserAgent", "Set HTTP User-Agent: header value" },
  { RAPTOR_FEATURE_JSON_CALLBACK     , AREA_SERIALIZER | VT_STRING, "jsonCallback", "JSON serializer callback" },
  { RAPTOR_FEATURE_JSON_EXTRA_DATA   , AREA_SERIALIZER | VT_STRING, "jsonExtraData", "JSON serializer extra data" },
  { RAPTOR_FEATURE_RSS_TRIPLES       , AREA_SERIALIZER | VT_STRING, "rssTriples", "Atom/RSS serializer writes extra RDF triples" },
  { RAPTOR_FEATURE_ATOM_ENTRY_URI    , AREA_SERIALIZER | VT_URI, "atomEntryUri", "Atom serializer Entry URI" },
  { RAPTOR_FEATURE_PREFIX_ELEMENTS   , AREA_SERIALIZER | VT_BOOL,  "prefixElements", "Atom/RSS serializers write namespace-prefixed elements" }
};


static const char * const raptor_feature_uri_prefix="http://feature.librdf.org/raptor-";
/* NOTE: this is strlen(raptor_feature_uri_prefix) */
#define RAPTOR_FEATURE_URI_PREFIX_LEN 33


/*
 * raptor_features_enumerate_common:
 * @world: raptor_world object
 * @feature: feature enumeration (0+)
 * @name: pointer to store feature short name (or NULL)
 * @uri: pointer to store feature URI (or NULL)
 * @label: pointer to feature label (or NULL)
 * @flags: flags to match
 * 
 * Internal: Get list of syntax features.
 *
 * If @uri is not NULL, a pointer toa new raptor_uri is returned
 * that must be freed by the caller with raptor_free_uri().
 *
 * Return value: 0 on success, <0 on failure, >0 if feature is unknown
 **/
int
raptor_features_enumerate_common(raptor_world* world,
                                 const raptor_feature feature,
                                 const char **name, 
                                 raptor_uri **uri, const char **label,
                                 int flags)
{
  int i;

  for(i = 0; i <= RAPTOR_FEATURE_LAST; i++)
    if(raptor_features_list[i].feature == feature &&
       (raptor_features_list[i].flags & flags)) {
      if(name)
        *name=raptor_features_list[i].name;
      
      if(uri) {
        raptor_uri *base_uri;
        base_uri = raptor_new_uri_from_counted_string(world,
                                                      (const unsigned char*)raptor_feature_uri_prefix,
                                                      RAPTOR_FEATURE_URI_PREFIX_LEN);
        if(!base_uri)
          return -1;
        
        *uri=raptor_new_uri_from_uri_local_name(world,
                                                   base_uri,
                                                   (const unsigned char*)raptor_features_list[i].name);
        raptor_free_uri(base_uri);
      }
      if(label)
        *label=raptor_features_list[i].label;
      return 0;
    }

  return 1;
}



/**
 * raptor_feature_value_type
 * @feature: raptor serializer or parser feature
 *
 * Get the type of a features.
 *
 * The type of the @feature is 0=integer , 1=string.  Other values are
 * undefined.  Most features are integer values and use
 * raptor_parser_set_feature and raptor_parser_get_feature()
 * ( raptor_serializer_set_feature raptor_serializer_get_feature() )
 *
 * String value features use raptor_parser_set_feature_string() and
 * raptor_parser_get_feature_string()
 * ( raptor_serializer_set_feature_string()
 * and raptor_serializer_get_feature_string() )
 *
 * Return value: the type of the feature or <0 if @feature is unknown
 */
int
raptor_feature_value_type(const raptor_feature feature) {
  if(feature > RAPTOR_FEATURE_LAST)
    return -1;
  return (raptor_features_list[feature].flags & 4) ? 1 : 0;
}


/**
 * raptor_world_get_feature_from_uri:
 * @world: raptor_world instance
 * @uri: feature URI
 *
 * Get a feature ID from a URI
 * 
 * The allowed feature URIs are available via raptor_features_enumerate().
 *
 * Return value: < 0 if the feature is unknown
 **/
raptor_feature
raptor_world_get_feature_from_uri(raptor_world* world, raptor_uri *uri)
{
  unsigned char *uri_string;
  int i;
  raptor_feature feature= (raptor_feature)-1;
  
  if(!uri)
    return feature;
  
  uri_string = raptor_uri_as_string(uri);
  if(strncmp((const char*)uri_string, raptor_feature_uri_prefix,
             RAPTOR_FEATURE_URI_PREFIX_LEN))
    return feature;

  uri_string += RAPTOR_FEATURE_URI_PREFIX_LEN;

  for(i = 0; i <= RAPTOR_FEATURE_LAST; i++)
    if(!strcmp(raptor_features_list[i].name, (const char*)uri_string)) {
      feature = (raptor_feature)i;
      break;
    }

  return feature;
}


/**
 * raptor_parser_get_feature_count:
 *
 * Get the count of features defined.
 *
 * This is prefered to the compile time-only symbol #RAPTOR_FEATURE_LAST
 * and returns a count of the number of features which is
 * #RAPTOR_FEATURE_LAST+1.
 *
 * Return value: count of features in the #raptor_feature enumeration
 **/
unsigned int
raptor_parser_get_feature_count(void) {
  return RAPTOR_FEATURE_LAST+1;
}

