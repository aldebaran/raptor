/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * ntriples_parse.c - Raptor N-Triples Parser implementation
 *
 * N-Triples
 * http://www.w3.org/TR/rdf-testcases/#ntriples
 *
 * Copyright (C) 2001-2010, David Beckett http://www.dajobe.org/
 * Copyright (C) 2001-2005, University of Bristol, UK http://www.bristol.ac.uk/
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Raptor includes */
#include "raptor2.h"
#include "raptor_internal.h"

/* Set RAPTOR_DEBUG to > 1 to get lots of buffer related debugging */
/*
#undef RAPTOR_DEBUG
#define RAPTOR_DEBUG 2
*/


/* Prototypes for local functions */
static void raptor_ntriples_generate_statement(raptor_parser* parser, raptor_term* subject_term, raptor_term* predicate_term, raptor_term* object_term, raptor_term* graph_term);

/*
 * NTriples parser object
 */
struct raptor_ntriples_parser_context_s {
  /* current line */
  unsigned char *line;
  /* current line length */
  size_t line_length;
  /* current char in line buffer */
  size_t offset;

  char last_char;
  
  /* static statement for use in passing to user code */
  raptor_statement statement;

  /* Non-0 if N-Quads */
  int is_nquads;

  int literal_graph_warning;
};


typedef struct raptor_ntriples_parser_context_s raptor_ntriples_parser_context;



/**
 * raptor_ntriples_parse_init:
 *
 * Initialise the Raptor NTriples parser.
 *
 * Return value: non 0 on failure
 **/

static int
raptor_ntriples_parse_init(raptor_parser* rdf_parser, const char *name)
{
  raptor_ntriples_parser_context *ntriples_parser;
  ntriples_parser = (raptor_ntriples_parser_context*)rdf_parser->context;

  raptor_statement_init(&ntriples_parser->statement, rdf_parser->world);

  if(!strcmp(name, "nquads"))
    ntriples_parser->is_nquads = 1;
  
  return 0;
}


/* PUBLIC FUNCTIONS */


/*
 * raptor_ntriples_parse_terminate - Free the Raptor NTriples parser
 * @rdf_parser: parser object
 * 
 **/
static void
raptor_ntriples_parse_terminate(raptor_parser* rdf_parser)
{
  raptor_ntriples_parser_context *ntriples_parser;
  ntriples_parser = (raptor_ntriples_parser_context*)rdf_parser->context;
  if(ntriples_parser->line_length)
    RAPTOR_FREE(cdata, ntriples_parser->line);
}


static void
raptor_ntriples_generate_statement(raptor_parser* parser, 
                                   raptor_term *subject,
                                   raptor_term *predicate,
                                   raptor_term *object,
                                   raptor_term *graph)
{
  /* raptor_ntriples_parser_context *ntriples_parser = (raptor_ntriples_parser_context*)parser->context; */
  raptor_statement *statement = &parser->statement;

  if(!parser->emitted_default_graph) {
    raptor_parser_start_graph(parser, NULL, 0);
    parser->emitted_default_graph++;
  }

  /* If there is no statement handler - there is nothing else to do */
  if(!parser->statement_handler)
    goto cleanup;

  statement->subject = subject;
  statement->predicate = predicate;
  statement->object = object;
  statement->graph = graph;

  /* Generate the statement */
  (*parser->statement_handler)(parser->user_data, statement);

  cleanup:
  raptor_free_statement(statement);
}


/* These are for 7-bit ASCII and not locale-specific */
#define IS_ASCII_ALPHA(c) (((c) > 0x40 && (c) < 0x5B) || ((c) > 0x60 && (c) < 0x7B))
#define IS_ASCII_UPPER(c) ((c) > 0x40 && (c) < 0x5B)
#define IS_ASCII_DIGIT(c) ((c) > 0x2F && (c) < 0x3A)
#define IS_ASCII_PRINT(c) ((c) > 0x1F && (c) < 0x7F)
#define TO_ASCII_LOWER(c) ((c)+0x20)

typedef enum {
  RAPTOR_TERM_CLASS_URI,      /* ends on > */
  RAPTOR_TERM_CLASS_BNODEID,  /* ends on first non [A-Za-z][A-Za-z0-9]* */
  RAPTOR_TERM_CLASS_STRING,   /* ends on non-escaped " */
  RAPTOR_TERM_CLASS_LANGUAGE  /* ends on first non [a-z0-9]+ ('-' [a-z0-9]+ )? */
} raptor_ntriples_term_class;


static int 
raptor_ntriples_term_valid(raptor_parser* rdf_parser,
                           unsigned char c, int position, 
                           raptor_ntriples_term_class term_class) 
{
  int result = 0;

  switch(term_class) {
    case RAPTOR_TERM_CLASS_URI:
      /* ends on > */
      result = (c != '>');
      break;
      
    case RAPTOR_TERM_CLASS_BNODEID:  
      /* ends on first non [A-Za-z0-9_:][-.A-Za-z0-9]* */
      result = IS_ASCII_ALPHA(c) || IS_ASCII_DIGIT(c) || c == '_' || c == ':';
      if(position)
        result = (result || c == '-' || c == '.');
      break;
      
    case RAPTOR_TERM_CLASS_STRING:
      /* ends on " */
      result = (c != '"');
      break;

    case RAPTOR_TERM_CLASS_LANGUAGE:
      /* ends on first non [a-zA-Z]+ ('-' [a-zA-Z0-9]+ )? */
      result = IS_ASCII_ALPHA(c);
      if(position)
        result = (result || IS_ASCII_DIGIT(c) || c == '-');
      break;
      
    default:
      raptor_parser_error(rdf_parser, "Unknown N-Triples term class %d",
                          term_class);
  }

  return result;
}


/*
 * raptor_ntriples_term:
 * @parser: NTriples parser
 * @start: pointer to starting character of string (in)
 * @dest: destination of string (in)
 * @lenp: pointer to length of string (in/out)
 * @dest_lenp: pointer to length of destination string (out)
 * @end_char: string ending character
 * @class: string class
 *
 * Parse an N-Triples term with escapes.
 *
 * Relies that @dest is long enough; it need only be as large as the
 * input string @start since when UTF-8 encoding, the escapes are
 * removed and the result is always less than or equal to length of
 * input.
 *
 * N-Triples strings/URIs are written in ASCII at present; characters
 * outside the printable ASCII range are discarded with a warning.
 * See the grammar for full details of the allowed ranges.
 *
 * UTF-8 and the \u and \U esapes are both allowed.
 *
 * Return value: Non 0 on failure
 **/
static int
raptor_ntriples_term(raptor_parser* rdf_parser, 
                     const unsigned char **start, unsigned char *dest, 
                     size_t *lenp, size_t *dest_lenp,
                     char end_char,
                     raptor_ntriples_term_class term_class)
{
  const unsigned char *p = *start;
  unsigned char c = '\0';
  size_t ulen = 0;
  unsigned long unichar = 0;
  unsigned int position = 0;
  int end_char_seen = 0;

  /* find end of string, fixing backslashed characters on the way */
  while(*lenp > 0) {
    int unichar_width;

    c = *p;

    p++;
    (*lenp)--;
    rdf_parser->locator.column++;
    rdf_parser->locator.byte++;

    if(c > 0x7f) {
      /* just copy the UTF-8 bytes through */
      int unichar_len;
      unichar_len = raptor_unicode_utf8_string_get_char(p - 1, 1 + *lenp, NULL);
      if(unichar_len < 0 || RAPTOR_GOOD_CAST(size_t, unichar_len) > *lenp) {
        raptor_parser_error(rdf_parser, "UTF-8 encoding error at character %d (0x%02X) found.", c, c);
        /* UTF-8 encoding had an error or ended in the middle of a string */
        return 1;
      }
      memmove(dest, p-1, unichar_len);
      dest += unichar_len;

      unichar_len--; /* p, *lenp were moved on by 1 earlier */
      
      p += unichar_len;
      (*lenp) -= unichar_len;
      rdf_parser->locator.column += unichar_len;
      rdf_parser->locator.byte += unichar_len;
      continue;
    }
    
    if(c != '\\') {
      /* finish at non-backslashed end_char */
      if(end_char && c == end_char) {
        end_char_seen = 1;
        break;
      }

      if(!raptor_ntriples_term_valid(rdf_parser, c, position, term_class)) {
        if(end_char) {
          /* end char was expected, so finding an invalid thing is an error */
          raptor_parser_error(rdf_parser, "Missing terminating '%c' (found '%c')", end_char, c);
          return 0;
        } else {
          /* it's the end - so rewind 1 to save next char */
          p--;
          (*lenp)++;
          rdf_parser->locator.column--;
          rdf_parser->locator.byte--;
          break;
        }
      }
      
      /* otherwise store and move on */
      *dest++ = c;
      position++;
      continue;
    }

    if(!*lenp) {
      raptor_parser_error(rdf_parser, "\\ at end of line");
      return 0;
    }

    c = *p;

    p++;
    (*lenp)--;
    rdf_parser->locator.column++;
    rdf_parser->locator.byte++;

    switch(c) {
      case '"':
      case '\\':
        *dest++ = c;
        break;
      case 'b':
        *dest++ = '\b';
        break;
      case 'f':
        *dest++ = '\f';
        break;
      case 'n':
        *dest++ = '\n';
        break;
      case 'r':
        *dest++ = '\r';
        break;
      case 't':
        *dest++ = '\t';
        break;
      case '<':
      case '>':
      case '{':
      case '}':
      case '|':
      case '^':
      case '`':
        /* Turtle 2013 allows these in URIs (as well as \" and \\) */
        *dest++ = c;
        break;

      case 'u':
      case 'U':
        ulen = (c == 'u') ? 4 : 8;
        
        if(*lenp < ulen) {
          raptor_parser_error(rdf_parser, "%c over end of line", c);
          return 0;
        }

        if(1) {
          unsigned int ii;
          int n = 0;

          for(ii = 0; ii < ulen; ii++) {
            char cc = p[ii];
            if(!isxdigit(RAPTOR_GOOD_CAST(char, cc))) {
              raptor_parser_error(rdf_parser,
                            "N-Triples string error - illegal hex digit %c in Unicode escape '%c%s...'",
                            cc, c, p);
              n = 1;
              break;
            }
          }

          if(n)
            break;
          
          n = sscanf((const char*)p, ((ulen == 4) ? "%04lx" : "%08lx"), &unichar);
          if(n != 1) {
            raptor_parser_error(rdf_parser, "Illegal Uncode escape '%c%s...'", c, p);
            break;
          }
        }

        p += ulen;
        (*lenp) -= ulen;
        rdf_parser->locator.column += RAPTOR_GOOD_CAST(int, ulen);
        rdf_parser->locator.byte += RAPTOR_GOOD_CAST(int, ulen);
        
        if(unichar > raptor_unicode_max_codepoint) {
          raptor_parser_error(rdf_parser,
                              "Illegal Unicode character with code point #x%lX (max #x%lX).",
                              unichar, raptor_unicode_max_codepoint);
          break;
        }

        unichar_width = raptor_unicode_utf8_string_put_char(unichar, dest, 4);
        if(unichar_width < 0) {
          raptor_parser_error(rdf_parser,
                              "Illegal Unicode character with code point #x%lX.", 
                              unichar);
          break;
        }

        /* The destination length is set here to 4 since we know that in 
         * all cases, the UTF-8 encoded output sequence is always shorter
         * than the input sequence, and the buffer is edited in place.
         *   \uXXXX: 6 bytes input - UTF-8 max 3 bytes output
         *   \uXXXXXXXX: 10 bytes input - UTF-8 max 4 bytes output
         */
        dest += (int)unichar_width;
        break;

      default:
        raptor_parser_error(rdf_parser,
                            "Illegal string escape \\%c in \"%s\"", c, 
                            (char*)start);
        return 0;
    }

    position++;
  } /* end while */

  
  if(end_char && !end_char_seen) {
    raptor_parser_error(rdf_parser, "Missing terminating '%c' before end of line.", end_char);
    return 1;
  }

  /* terminate dest, can be shorter than source */
  *dest = '\0';

  if(dest_lenp)
    *dest_lenp = p - *start;

  *start = p;

  return 0;
}


#define MAX_NTRIPLES_TERMS 4

static int
raptor_ntriples_parse_line(raptor_parser* rdf_parser,
                           unsigned char *buffer, size_t len,
                           int max_terms)
{
  raptor_ntriples_parser_context *ntriples_parser = (raptor_ntriples_parser_context*)rdf_parser->context;
  int i;
  unsigned char *p;
  unsigned char *dest;
  raptor_term* real_terms[MAX_NTRIPLES_TERMS] = {NULL, NULL, NULL, NULL};
  size_t term_length = 0;
  int rc = 0;
  
  /* ASSERTION:
   * p always points to first char we are considering
   * p[len-1] always points to last char
   */
  
  /* Handle empty  lines */
  if(!len)
    return 0;

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
  RAPTOR_DEBUG3("handling line '%s' (%d bytes)\n", buffer, (unsigned int)len);
#endif
  
  p = buffer;

  while(len > 0 && isspace((int)*p)) {
    p++;
    rdf_parser->locator.column++;
    rdf_parser->locator.byte++;
    len--;
  }

  /* Handle empty - all whitespace lines */
  if(!len)
    return 0;
  
  /* Handle comment lines */
  if(*p == '#')
    return 0;
  
  /* Remove trailing spaces */
  while(len > 0 && isspace((int)p[len-1])) {
    p[len-1] = '\0';
    len--;
  }

  /* can't be empty now - that would have been caught above */
  
  /* Must be triple/quad */

  for(i = 0; i < 4; i++) {
    if(!len) {
      /* context is optional in nquads */
      if (i == 3)
	break;
      raptor_parser_error(rdf_parser, "Unexpected end of line");
      goto cleanup;
    }
    

    if(i == 2) {
      /* object term (2): expect either <URI> or _:name or literal */
      if(*p != '<' && *p != '_' && *p != '"' && *p != 'x') {
        raptor_parser_error(rdf_parser, "Saw '%c', expected <URIref>, _:bnodeID or \"literal\"", *p);
        goto cleanup;
      }
    } else if(i == 1) {
      /* predicate term (1): expect URI only */
      if(*p != '<') {
        raptor_parser_error(rdf_parser, "Saw '%c', expected <URIref>", *p);
        goto cleanup;
      }
    } else {
      /* subject (0) or graph (3) terms: expect <URI> or _:name */
      if(*p != '<' && *p != '_') {
        raptor_parser_error(rdf_parser, "Saw '%c', expected <URIref> or _:bnodeID", *p);
        goto cleanup;
      }
    }


    switch(*p) {
      case '<':
        dest = p;

        p++;
        len--;
        rdf_parser->locator.column++;
        rdf_parser->locator.byte++;

        if(raptor_ntriples_term(rdf_parser,
                                (const unsigned char**)&p, 
                                dest, &len, &term_length, 
                                '>', RAPTOR_TERM_CLASS_URI)) {
          rc = 1;
          goto cleanup;
        }

        if(!raptor_turtle_check_uri_string(dest)) {
          raptor_parser_error(rdf_parser, "URI '%s' contains bad character(s)", dest);
          rc = 1;
          goto cleanup;
        }

        if(1) {
          raptor_uri *uri;

          /* Check for bad ordinal predicate */
          if(!strncmp((const char*)dest,
                      "http://www.w3.org/1999/02/22-rdf-syntax-ns#_", 44)) {
            int ordinal = raptor_check_ordinal(dest + 44);
            if(ordinal <= 0)
              raptor_parser_error(rdf_parser, "Illegal ordinal value %d in property '%s'.", ordinal, dest);
          }
          if(raptor_uri_uri_string_is_absolute(dest) <= 0) {
            raptor_parser_error(rdf_parser, "URI '%s' is not absolute.", dest);
            goto cleanup;
          }
          
          uri = raptor_new_uri(rdf_parser->world, dest);
          if(!uri) {
            raptor_parser_error(rdf_parser, "Could not create URI for '%s'", (const char *)dest);
            goto cleanup;
          }
          real_terms[i] = raptor_new_term_from_uri(rdf_parser->world, uri);
          raptor_free_uri(uri);
        }
        break;

      case '"':
        dest = p;

        p++;
        len--;
        rdf_parser->locator.column++;
        rdf_parser->locator.byte++;

        if(raptor_ntriples_term(rdf_parser,
                                (const unsigned char**)&p,
                                dest, &len, &term_length,
                                '"', RAPTOR_TERM_CLASS_STRING)) {
          rc = 1;
          goto cleanup;
        }

        if(1) {
          unsigned char *object_literal_language = NULL;
          unsigned char *object_literal_datatype = NULL;
          raptor_uri* datatype_uri = NULL;
    
          if(len && *p == '@') {
            unsigned char *q;
            size_t lang_len;
            
            object_literal_language = p;

            /* Skip - */
            p++;
            len--;
            rdf_parser->locator.column++;
            rdf_parser->locator.byte++;

            if(!len) {
              raptor_parser_error(rdf_parser, "Missing language after \"string\"-");
              goto cleanup;
            }


            if(raptor_ntriples_term(rdf_parser,
                                    (const unsigned char**)&p,
                                    object_literal_language, &len, &lang_len,
                                    '\0', RAPTOR_TERM_CLASS_LANGUAGE)) {
              rc = 1;
              goto cleanup;
            }

            if(!lang_len) {
              raptor_parser_error(rdf_parser, "Invalid language tag at @%s", p);
              rc = 1;
              goto cleanup;
            }
            
            /* Normalize language to lowercase
             * http://www.w3.org/TR/rdf-concepts/#dfn-language-identifier
             */
            for(q = object_literal_language; *q; q++) {
              if(IS_ASCII_UPPER(*q))
                *q = TO_ASCII_LOWER(*q);
            }

          }

          if(len >1 && *p == '^' && p[1] == '^') {

            object_literal_datatype = p;

            /* Skip ^^ */
            p += 2;
            len -= 2;
            rdf_parser->locator.column += 2;
            rdf_parser->locator.byte += 2;

            if(!len || (len && *p != '<')) {
              raptor_parser_error(rdf_parser, "Missing datatype URI-ref in\"string\"^^<URI-ref> after ^^");
              goto cleanup;
            }

            p++;
            len--;
            rdf_parser->locator.column++;
            rdf_parser->locator.byte++;

            if(raptor_ntriples_term(rdf_parser,
                                    (const unsigned char**)&p,
                                    object_literal_datatype, &len, NULL,
                                    '>', RAPTOR_TERM_CLASS_URI)) {
              rc = 1;
              goto cleanup;
            }

            if(raptor_uri_uri_string_is_absolute(object_literal_datatype) <= 0) {
              raptor_parser_error(rdf_parser, "Datatype URI '%s' is not absolute.", object_literal_datatype);
              rc = 1;
              goto cleanup;
            }

          }

          if(object_literal_datatype && object_literal_language) {
            raptor_parser_warning(rdf_parser, "Typed literal used with a language - ignoring the language");
            object_literal_language = NULL;
          }

          if(object_literal_datatype) {
            datatype_uri = raptor_new_uri(rdf_parser->world,
                                          object_literal_datatype);
            if(!datatype_uri) {
              raptor_parser_error(rdf_parser, "Could not create literal datatype uri '%s'", object_literal_datatype);
              goto cleanup;
            }
            object_literal_language = NULL;
          }
          
          real_terms[i] = raptor_new_term_from_literal(rdf_parser->world,
                                                       dest,
                                                       datatype_uri,
                                                       object_literal_language);
        }

        break;


      case '_':
        /* store where _ was */
        dest = p;

        p++;
        len--;
        rdf_parser->locator.column++;
        rdf_parser->locator.byte++;

        if(!len || (len > 0 && *p != ':')) {
          raptor_parser_error(rdf_parser, "Illegal bNodeID - _ not followed by :");
          goto cleanup;
        }

        /* Found ':' - move on */

        p++;
        len--;
        rdf_parser->locator.column++;
        rdf_parser->locator.byte++;

        if(raptor_ntriples_term(rdf_parser,
                                (const unsigned char**)&p,
                                dest, &len, &term_length,
                                '\0', RAPTOR_TERM_CLASS_BNODEID)) {
          rc = 1;
          goto cleanup;
        }

        if(!term_length) {
          raptor_parser_error(rdf_parser, "Bad or missing bNodeID after _:");
          goto cleanup;
        }

        real_terms[i] = raptor_new_term_from_blank(rdf_parser->world, dest);

        break;

      default:
        raptor_parser_fatal_error(rdf_parser, "Unknown term type");
        rc = 1;
        goto cleanup;
    }


    /* Skip whitespace after terms */
    while(len > 0 && isspace((int)*p)) {
      p++;
      len--;
      rdf_parser->locator.column++;
      rdf_parser->locator.byte++;
    }

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
    if(1) {
      unsigned char* c = raptor_term_to_string(real_terms[i]);
      fprintf(stderr, "item %d: term '%s' type %d\n",
              i, c, real_terms[i]->type);
      raptor_free_string(c);
    }
#endif

    /* Look for terminating '.' after 3rd (ntriples) or 3rd/4th (nquads) term */
    if(i == (ntriples_parser->is_nquads ? 4 : 3) && *p != '.') {
      raptor_parser_error(rdf_parser, "Missing terminating \".\"");
      return 0;
    }

    /* Still may be optional so check again */
    if(*p == '.') {
      p++;
      len--;
      rdf_parser->locator.column++;
      rdf_parser->locator.byte++;

      /* Skip whitespace after '.' */
      while(len > 0 && isspace((int)*p)) {
        p++;
        len--;
        rdf_parser->locator.column++;
        rdf_parser->locator.byte++;
      }

      /* Only a comment is allowed here */
      if(*p && *p != '#') {
        raptor_parser_error(rdf_parser, "Junk after terminating \".\"");
        return 0;
      }

      p += len; len = 0;
    }
  }


  /* Check N-Triples has only 3 terms */
  if(!ntriples_parser->is_nquads) {
    if(real_terms[3]) {
      raptor_free_term(real_terms[3]);
      real_terms[3] = NULL;
      raptor_parser_error(rdf_parser, "N-Triples only allows 3 terms");
      goto cleanup;
    }
  }

  if(real_terms[3] && real_terms[3]->type == RAPTOR_TERM_TYPE_LITERAL) {
    if(!ntriples_parser->literal_graph_warning++)
      raptor_parser_warning(rdf_parser, "Ignoring N-Quad literal contexts");

    raptor_free_term(real_terms[3]);
    real_terms[3] = NULL;
  }

  raptor_ntriples_generate_statement(rdf_parser, 
                                     real_terms[0],
                                     real_terms[1],
                                     real_terms[2],
                                     real_terms[3]);

  rdf_parser->locator.byte += RAPTOR_BAD_CAST(int, len);

 cleanup:

  return rc;
}


static int
raptor_ntriples_parse_chunk(raptor_parser* rdf_parser, 
                            const unsigned char *s, size_t len,
                            int is_end)
{
  unsigned char *buffer;
  unsigned char *ptr;
  unsigned char *start;
  raptor_ntriples_parser_context *ntriples_parser = (raptor_ntriples_parser_context*)rdf_parser->context;
  int max_terms = ntriples_parser->is_nquads ? 4 : 3;
  
#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
  RAPTOR_DEBUG2("adding %d bytes to buffer\n", (unsigned int)len);
#endif

  /* No data?  It's the end */
  if(!len)
    return 0;

  buffer = RAPTOR_MALLOC(unsigned char*, ntriples_parser->line_length + len + 1);
  if(!buffer) {
    raptor_parser_fatal_error(rdf_parser, "Out of memory");
    return 1;
  }

  if(ntriples_parser->line_length) {
    memcpy(buffer, ntriples_parser->line, ntriples_parser->line_length);
    RAPTOR_FREE(char*, ntriples_parser->line);
  }

  ntriples_parser->line = buffer;

  /* move pointer to end of cdata buffer */
  ptr = buffer+ntriples_parser->line_length;

  /* adjust stored length */
  ntriples_parser->line_length += len;

  /* now write new stuff at end of cdata buffer */
  memcpy(ptr, s, len);
  ptr += len;
  *ptr = '\0';

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
  RAPTOR_DEBUG2("buffer now %d bytes\n", ntriples_parser->line_length);
#endif

  ptr = buffer+ntriples_parser->offset;
  while(*(start = ptr)) {
    unsigned char *line_start = ptr;
    
#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
  RAPTOR_DEBUG3("line buffer now '%s' (offset %d)\n", ptr, ptr-(buffer+ntriples_parser->offset));
#endif

    /* skip \n when just seen \r - i.e. \r\n or CR LF */
    if(ntriples_parser->last_char == '\r' && *ptr == '\n') {
#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
      RAPTOR_DEBUG1("skipping a \\n\n");
#endif
      ptr++;
      rdf_parser->locator.byte++;
      rdf_parser->locator.column = 0;
      start = line_start = ptr;
    }

    while(*ptr && *ptr != '\n' && *ptr != '\r')
      ptr++;

    if(!*ptr)
      break;

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
    RAPTOR_DEBUG3("found newline \\x%02x at offset %d\n", *ptr,
                  ptr-line_start);
#endif
    ntriples_parser->last_char = *ptr;

    len = ptr-line_start;
    rdf_parser->locator.column = 0;

    *ptr = '\0';
    if(raptor_ntriples_parse_line(rdf_parser, line_start, len, max_terms))
      return 1;
    
    rdf_parser->locator.line++;

    /* go past newline */
    ptr++;
    rdf_parser->locator.byte++;

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
    /* Do not peek if too far */
    if(ptr-buffer < ntriples_parser->line_length)
      RAPTOR_DEBUG2("next char is \\x%02x\n", *ptr);
    else
      RAPTOR_DEBUG1("next char unknown - end of buffer\n");
#endif
  }

  ntriples_parser->offset = start - buffer;

  len = ntriples_parser->line_length - ntriples_parser->offset;
    
  if(len) {
    /* collapse buffer */

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
    RAPTOR_DEBUG3("collapsing buffer from %d to %d bytes\n", ntriples_parser->line_length, (unsigned int)len);
#endif
    buffer = RAPTOR_MALLOC(unsigned char*, len + 1);
    if(!buffer) {
      raptor_parser_fatal_error(rdf_parser, "Out of memory");
      return 1;
    }

    memcpy(buffer, 
           ntriples_parser->line + ntriples_parser->line_length - len,
           len);
    buffer[len] = '\0';

    RAPTOR_FREE(char*, ntriples_parser->line);

    ntriples_parser->line = buffer;
    ntriples_parser->line_length -= ntriples_parser->offset;
    ntriples_parser->offset = 0;

#if defined(RAPTOR_DEBUG) && RAPTOR_DEBUG > 1
    RAPTOR_DEBUG3("buffer now '%s' (%d bytes)\n", ntriples_parser->line, ntriples_parser->line_length);
#endif    
  }
  
  /* exit now, no more input */
  if(is_end) {
    if(ntriples_parser->offset != ntriples_parser->line_length) {
       raptor_parser_error(rdf_parser, "Junk at end of input.\"");
       return 1;
    }
    
    if(rdf_parser->emitted_default_graph) {
      raptor_parser_end_graph(rdf_parser, NULL, 0);
      rdf_parser->emitted_default_graph--;
    }

    return 0;
  }
    
  return 0;
}


static int
raptor_ntriples_parse_start(raptor_parser* rdf_parser) 
{
  raptor_locator *locator = &rdf_parser->locator;
  raptor_ntriples_parser_context *ntriples_parser = (raptor_ntriples_parser_context*)rdf_parser->context;

  locator->line = 1;
  locator->column = 0;
  locator->byte = 0;

  ntriples_parser->last_char = '\0';

  return 0;
}


#if defined RAPTOR_PARSER_NTRIPLES || defined RAPTOR_PARSER_NQUADS
static int
raptor_ntriples_parse_recognise_syntax(raptor_parser_factory* factory, 
                                       const unsigned char *buffer, size_t len,
                                       const unsigned char *identifier, 
                                       const unsigned char *suffix, 
                                       const char *mime_type)
{
  int score = 0;
  
  if(suffix) {
    if(!strcmp((const char*)suffix, "nt"))
      score = 8;

    /* Explicitly refuse to do anything with Turtle or N3 named content */
    if(!strcmp((const char*)suffix, "ttl") ||
       !strcmp((const char*)suffix, "n3")) {
      return 0;
    }
  }
  
  if(mime_type) {
    if(strstr((const char*)mime_type, "ntriples"))
      score += 6;
  }
  
  if(buffer && len) {
    int has_ntriples_3;

    /* recognizing N-Triples is tricky but rely that it is line based
     * and that all URLs are absolute, and there are a lot of http:
     * URLs
     */
#define  HAS_AT_PREFIX (raptor_memstr((const char*)buffer, len, "@prefix ") != NULL)

#define  HAS_NTRIPLES_START_1_LEN 8
#define  HAS_NTRIPLES_START_1 (!memcmp((const char*)buffer, "<http://", HAS_NTRIPLES_START_1_LEN))
#define  HAS_NTRIPLES_START_2_LEN 2
#define  HAS_NTRIPLES_START_2 (!memcmp((const char*)buffer, "_:", HAS_NTRIPLES_START_2_LEN))

#define  HAS_NTRIPLES_1 (raptor_memstr((const char*)buffer, len, "\n<http://") != NULL)
#define  HAS_NTRIPLES_2 (raptor_memstr((const char*)buffer, len, "\r<http://") != NULL)
#define  HAS_NTRIPLES_3 (raptor_memstr((const char*)buffer, len, "> <http://") != NULL)
#define  HAS_NTRIPLES_4 (raptor_memstr((const char*)buffer, len, "> <") != NULL)
#define  HAS_NTRIPLES_5 (raptor_memstr((const char*)buffer, len, "> \"") != NULL)
    if(HAS_AT_PREFIX)
      /* Turtle */
      return 0;

    has_ntriples_3 = HAS_NTRIPLES_3;

    /* Bonus if the first bytes look N-Triples-like */
    if(len >= HAS_NTRIPLES_START_1_LEN && HAS_NTRIPLES_START_1)
      score++;
    if(len >= HAS_NTRIPLES_START_2_LEN && HAS_NTRIPLES_START_2)
      score++;

    if(HAS_NTRIPLES_1 || HAS_NTRIPLES_2) {
      /* N-Triples file with newlines and HTTP subjects */
      score += 6;
      if(has_ntriples_3)
        score++;
    } else if(has_ntriples_3) {
      /* an HTTP URL predicate or object but no HTTP subject */
      score += 3;
    } else if(HAS_NTRIPLES_4) {
      /* non HTTP urls - weak check */
      score += 2;
      if(HAS_NTRIPLES_5)
        /* bonus for a literal object */
        score++;
    }
  }

  return score;
}


static const char* const ntriples_names[2] = { "ntriples", NULL };

static const char* const ntriples_uri_strings[3] = {
  "http://www.w3.org/ns/formats/N-Triples",
  "http://www.w3.org/TR/rdf-testcases/#ntriples",
  NULL
};
  
#define NTRIPLES_TYPES_COUNT 2
static const raptor_type_q ntriples_types[NTRIPLES_TYPES_COUNT + 1] = {
  { "application/n-triples", 21, 10},
  { "text/plain", 10, 1},
  { NULL, 0, 0}
};

static int
raptor_ntriples_parser_register_factory(raptor_parser_factory *factory) 
{
  int rc = 0;

  factory->desc.names = ntriples_names;

  factory->desc.mime_types = ntriples_types;

  factory->desc.label = "N-Triples";
  factory->desc.uri_strings = ntriples_uri_strings;

  factory->desc.flags = 0;
  
  factory->context_length     = sizeof(raptor_ntriples_parser_context);

  factory->init      = raptor_ntriples_parse_init;
  factory->terminate = raptor_ntriples_parse_terminate;
  factory->start     = raptor_ntriples_parse_start;
  factory->chunk     = raptor_ntriples_parse_chunk;
  factory->recognise_syntax = raptor_ntriples_parse_recognise_syntax;

  return rc;
}


int
raptor_init_parser_ntriples(raptor_world* world)
{
  return !raptor_world_register_parser_factory(world,
                                               &raptor_ntriples_parser_register_factory);
}

#endif


#ifdef RAPTOR_PARSER_NQUADS
static int
raptor_nquads_parse_recognise_syntax(raptor_parser_factory* factory, 
                                     const unsigned char *buffer, size_t len,
                                     const unsigned char *identifier, 
                                     const unsigned char *suffix, 
                                     const char *mime_type)
{
  int score = 0;
  int ntriples_score;
  
  if(suffix) {
    if(!strcmp((const char*)suffix, "nq"))
      score = 2;

    /* Explicitly refuse to do anything with N-Triples, Turtle or N3
     * named content 
     */
    if(!strcmp((const char*)suffix, "nt") ||
       !strcmp((const char*)suffix, "ttl") ||
       !strcmp((const char*)suffix, "n3")) {
      return 0;
    }
  }
  
  if(mime_type) {
    if(strstr((const char*)mime_type, "nquads"))
      score += 2;
  }
  
  /* ntriples is a subset of nquads, score higher than ntriples */
  ntriples_score = raptor_ntriples_parse_recognise_syntax(factory, buffer, len, identifier, suffix, mime_type);
  if(ntriples_score > 0) {
    score += ntriples_score + 1;
  }

  return score;
}


static const char* const nquads_names[2] = { "nquads", NULL };

static const char* const nquads_uri_strings[2] = {
  "http://sw.deri.org/2008/07/n-quads/",
  NULL
};
  
#define NQUADS_TYPES_COUNT 1
static const raptor_type_q nquads_types[NQUADS_TYPES_COUNT + 1] = {
  { "text/x-nquads", 13, 10},
  { NULL, 0, 0}
};

static int
raptor_nquads_parser_register_factory(raptor_parser_factory *factory) 
{
  int rc = 0;

  factory->desc.names = nquads_names;

  factory->desc.mime_types = nquads_types;

  factory->desc.label = "N-Quads";
  factory->desc.uri_strings = nquads_uri_strings;

  factory->desc.flags = 0;
  
  factory->context_length     = sizeof(raptor_ntriples_parser_context);

  factory->init      = raptor_ntriples_parse_init;
  factory->terminate = raptor_ntriples_parse_terminate;
  factory->start     = raptor_ntriples_parse_start;
  factory->chunk     = raptor_ntriples_parse_chunk;
  factory->recognise_syntax = raptor_nquads_parse_recognise_syntax;

  return rc;
}


int
raptor_init_parser_nquads(raptor_world* world)
{
  return !raptor_world_register_parser_factory(world,
                                               &raptor_nquads_parser_register_factory);
}
#endif
