/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-CurrentYear, Linköpings University,
 * Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF THIS OSMC PUBLIC
 * LICENSE (OSMC-PL). ANY USE, REPRODUCTION OR DISTRIBUTION OF
 * THIS PROGRAM CONSTITUTES RECIPIENT'S ACCEPTANCE OF THE OSMC
 * PUBLIC LICENSE.
 *
 * The OpenModelica software and the Open Source Modelica
 * Consortium (OSMC) Public License (OSMC-PL) are obtained
 * from Linköpings University, either from the above address,
 * from the URL: http://www.ida.liu.se/projects/OpenModelica
 * and in the OpenModelica distribution.
 *
 * This program is distributed  WITHOUT ANY WARRANTY; without
 * even the implied warranty of  MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE, EXCEPT AS EXPRESSLY SET FORTH
 * IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE CONDITIONS
 * OF OSMC-PL.
 *
 * See the full OSMC Public License conditions for more details.
 *
 */

/* Include standard headers before we do odd things with the __cplusplus define */

#if defined(_WIN32)
#include <winsock2.h>
#undef PURE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define bool int

#include <julia.h>

#include "MetaModelicaJuliaLayer.h"
#include "OpenModelicaJuliaHeader.h"

#include <Modelica_3_Lexer.h>
#include <ModelicaParser.h>
#include <antlr3intstream.h>
#include <antlr3config.h>

pthread_once_t parser_once_create_key = PTHREAD_ONCE_INIT;
pthread_key_t modelicaParserKey;

static void make_key()
{
  pthread_key_create(&modelicaParserKey,NULL);
}

static void lexNoRecover(pANTLR3_LEXER lexer)
{
  pANTLR3_INT_STREAM inputStream = (pANTLR3_INT_STREAM) NULL;
  lexer->rec->state->error = ANTLR3_TRUE;
  lexer->rec->state->failed = ANTLR3_TRUE;
  inputStream = lexer->input->istream;
  inputStream->consume(inputStream);
}

static void noRecover(pANTLR3_BASE_RECOGNIZER recognizer)
{
  recognizer->state->error = ANTLR3_TRUE;
  recognizer->state->failed = ANTLR3_TRUE;
}

static void* noRecoverFromMismatchedSet(pANTLR3_BASE_RECOGNIZER recognizer, pANTLR3_BITSET_LIST follow)
{
  recognizer->state->error  = ANTLR3_TRUE;
  recognizer->state->failed = ANTLR3_TRUE;
  return NULL;
}

static void* noRecoverFromMismatchedToken(pANTLR3_BASE_RECOGNIZER recognizer, ANTLR3_UINT32 ttype, pANTLR3_BITSET_LIST follow)
{
  pANTLR3_PARSER        parser;
  pANTLR3_TREE_PARSER        tparser;
  pANTLR3_INT_STREAM        is;
  void          * matchedSymbol;

  // Invoke the debugger event if there is a debugger listening to us
  //
  if  (recognizer->debugger != NULL)
  {
    recognizer->debugger->recognitionException(recognizer->debugger, recognizer->state->exception);
  }

  switch  (recognizer->type)
  {
  case  ANTLR3_TYPE_PARSER:

    parser  = (pANTLR3_PARSER) (recognizer->super);
    tparser = (pANTLR3_TREE_PARSER) NULL;
    is  = parser->tstream->istream;

    break;

  case  ANTLR3_TYPE_TREE_PARSER:

    tparser = (pANTLR3_TREE_PARSER) (recognizer->super);
    parser  = (pANTLR3_PARSER) NULL;
    is  = tparser->ctnstream->tnstream->istream;

    break;

  default:

    ANTLR3_FPRINTF(stderr, "Base recognizer function recoverFromMismatchedToken called by unknown parser type - provide override for this function\n");
    return NULL;

    break;
  }
  recognizer->state->failed = ANTLR3_TRUE;

  // Create an exception if we need one
  //
  if  (recognizer->state->exception == NULL)
  {
    antlr3RecognitionExceptionNew(recognizer);
  }

  if  ( recognizer->mismatchIsUnwantedToken(recognizer, is, ttype) == ANTLR3_TRUE)
  {
    recognizer->state->exception->type    = ANTLR3_UNWANTED_TOKEN_EXCEPTION;
    recognizer->state->exception->message  = (void*)ANTLR3_UNWANTED_TOKEN_EXCEPTION_NAME;
    recognizer->state->exception->expecting  = ttype;
    return NULL;
  }

  if  (recognizer->mismatchIsMissingToken(recognizer, is, follow))
  {
    matchedSymbol = recognizer->getMissingSymbol(recognizer, is, recognizer->state->exception, ttype, follow);
    recognizer->state->exception->type    = ANTLR3_MISSING_TOKEN_EXCEPTION;
    recognizer->state->exception->message  = (void*)ANTLR3_MISSING_TOKEN_EXCEPTION_NAME;
    recognizer->state->exception->token    = matchedSymbol;
    recognizer->state->exception->expecting  = ttype;
    return NULL;
  }

  // Neither deleting nor inserting tokens allows recovery
  // must just report the exception.
  //
  recognizer->state->error      = ANTLR3_TRUE;
  return NULL;
}

static void handleLexerError(pANTLR3_BASE_RECOGNIZER recognizer, pANTLR3_UINT8 * tokenNames)
{
  pANTLR3_LEXER lexer = (pANTLR3_LEXER)(recognizer->super);
  int isEOF = lexer->input->istream->_LA(lexer->input->istream, 1) == -1;
  char* chars[] = {
    isEOF ? strdup("<EOF>") : strdup((const char*)(lexer->input->substr(lexer->input, lexer->getCharIndex(lexer), lexer->getCharIndex(lexer)+10)->chars)),
    strdup((const char*)lexer->getText(lexer)->chars)
  };
  int line = 0;
  int offset = 0;

  if (strlen(chars[1]) > 20)
    chars[1][20] = '\0';
  line = lexer->getLine(lexer);
  offset = lexer->getCharPositionInLine(lexer)+1;
  if (*chars[1] && !ModelicaParser_lexerError) {
    c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Lexer got '%s' but failed to recognize the rest: '%s'", (const char**) chars, 2, line, offset, line, offset, false, ModelicaParser_filename_C_testsuiteFriendly);
  } else if (!ModelicaParser_lexerError) {
    c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Lexer failed to recognize '%s'", (const char**) chars, 1, line, offset, line, offset, false, ModelicaParser_filename_C_testsuiteFriendly);
  }
  ModelicaParser_lexerError = ANTLR3_TRUE;
  free(chars[0]);
  free(chars[1]);
}

#include "lookupTokenName.c"

/* Error handling based on antlr3baserecognizer.c */
static void handleParseError(pANTLR3_BASE_RECOGNIZER recognizer, pANTLR3_UINT8 * tokenNames)
{
  pANTLR3_PARSER      parser;
  pANTLR3_EXCEPTION      ex;
  pANTLR3_COMMON_TOKEN   preToken,nextToken;
  pANTLR3_TOKEN_STREAM tokenStream;
  int type;
  const char *token_text[3] = {0,0,0};
  int p_offset, n_offset, p_line, n_line;
  recognizer->state->error = ANTLR3_TRUE;
  recognizer->state->failed = ANTLR3_TRUE;

  if (ModelicaParser_lexerError)
    return;

  // Retrieve some info for easy reading.
  ex      =    recognizer->state->exception;

  switch  (recognizer->type)
  {
  case  ANTLR3_TYPE_PARSER:
    parser = (pANTLR3_PARSER) (recognizer->super);
    token_text[1] = (const char*) ex->message;
    type = ex->type;
    tokenStream = parser->getTokenStream(parser);
    preToken = tokenStream->_LT(tokenStream,1);
    nextToken = tokenStream->_LT(tokenStream,2);
    if (preToken == NULL) preToken = nextToken;
    p_line = preToken->line;
    n_line = nextToken->line;
    p_offset = preToken->charPosition+1;
    n_offset = nextToken->charPosition;
    break;

  default:

    ANTLR3_FPRINTF(stderr, "Base recognizer function displayRecognitionError called by unknown parser type - provide override for this function\n");
    return;
    break;
  }

  switch (type) {
  case ANTLR3_UNWANTED_TOKEN_EXCEPTION:
    {
      ANTLR3_COMMON_TOKEN *token = (ANTLR3_COMMON_TOKEN*) ex->token;
      ANTLR3_STRING *str = token->getText(token);
      token_text[0] = lookupTokenName(token->type,tokenNames);
      token_text[1] = token->type == ANTLR3_TOKEN_EOF ? "" : (const char*) str->chars;
      token_text[2] = lookupTokenName(ex->expecting,tokenNames);
      c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Expected token of type %s, got '%s' of type %s", token_text, 3, p_line, p_offset, n_line, n_offset, false, ModelicaParser_filename_C_testsuiteFriendly);
      break;
    }
  case ANTLR3_MISSING_TOKEN_EXCEPTION:
    token_text[0] = lookupTokenName(ex->expecting,tokenNames);
    c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Missing token: %s", token_text, 1, p_line, p_offset, p_line, p_offset, false, ModelicaParser_filename_C_testsuiteFriendly);
    break;
  case ANTLR3_NO_VIABLE_ALT_EXCEPTION:
    token_text[0] = preToken->type == ANTLR3_TOKEN_EOF ? "<EOF>" : (const char*)preToken->getText(preToken)->chars;
    if (preToken->type == ANTLR3_TOKEN_EOF) n_offset = p_offset;
    c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "No viable alternative near token: %s", token_text, 1, p_line, p_offset, n_line, n_offset, false, ModelicaParser_filename_C_testsuiteFriendly);
    break;
  case ModelicaParserException:
    {
      fileinfo* info = (fileinfo*) ex->custom;
      c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Parse error: %s", token_text+1, 1, info->line1, info->offset1, info->line2, info->offset2, false, ModelicaParser_filename_C_testsuiteFriendly);
      free(info);
      ex->custom = 0;
      break;
    }
  case ANTLR3_MISMATCHED_SET_EXCEPTION:
  case ANTLR3_EARLY_EXIT_EXCEPTION:
  case ANTLR3_RECOGNITION_EXCEPTION:
  default:
    token_text[2] = (const char*)ex->message;
    token_text[1] = preToken->type == ANTLR3_TOKEN_EOF ? "" : (const char*)preToken->getText(preToken)->chars;
    token_text[0] = lookupTokenName(preToken->type,tokenNames);
    if (preToken->type == ANTLR3_TOKEN_EOF) n_offset = p_offset;
    c_add_source_message(NULL,2, ErrorType_syntax, ErrorLevel_error, "Parser error: %s near: %s (%s)", token_text, 3, p_line, p_offset, n_line, n_offset, false, ModelicaParser_filename_C_testsuiteFriendly);
    break;
  }

}

static void* parseStream(pANTLR3_INPUT_STREAM input)
{
  pANTLR3_LEXER               pLexer;
  pANTLR3_COMMON_TOKEN_STREAM tstream;
  pModelicaParser             psr;
  void* lxr = 0;
  void* res = NULL;

  jl_gc_enable(1);

  OpenModelica_initMetaModelicaJuliaLayer();
  OpenModelica_initAbsynReferences();

  lxr = Modelica_3_LexerNew(input);
  if (lxr == NULL ) { fprintf(stderr, "Unable to create the lexer due to malloc() failure1\n"); fflush(stderr); exit(ANTLR3_ERR_NOMEM); }
  pLexer = ((pModelica_3_Lexer)lxr)->pLexer;
  pLexer->rec->displayRecognitionError = handleLexerError;
  pLexer->recover = lexNoRecover;
  tstream = antlr3CommonTokenStreamSourceNew(ANTLR3_SIZE_HINT, TOKENSOURCE(((pModelica_3_Lexer)lxr)));

  ModelicaParser_lexerError = ANTLR3_FALSE;

  if (tstream == NULL) { fprintf(stderr, "Out of memory trying to allocate token stream\n"); fflush(stderr); exit(ANTLR3_ERR_NOMEM); }
  tstream->channel = ANTLR3_TOKEN_DEFAULT_CHANNEL;
  tstream->discardOffChannel = ANTLR3_TRUE;
  tstream->discardOffChannelToks(tstream, ANTLR3_FALSE);

  // Finally, now that we have our lexer constructed, create the parser
  psr      = ModelicaParserNew(tstream);  // ModelicaParserNew is generated by ANTLR3

  psr->pModelicaParser_omcTop = psr->pModelicaParser_omcPush(psr);
  psr->pModelicaParser_omcTop->numPushed = 0;

  if (tstream == NULL) { fprintf(stderr, "Out of memory trying to allocate parser\n"); fflush(stderr); exit(ANTLR3_ERR_NOMEM); }

  psr->pParser->rec->displayRecognitionError = handleParseError;
  psr->pParser->rec->recover = noRecover;
  psr->pParser->rec->recoverFromMismatchedToken = noRecoverFromMismatchedToken;

  res = psr->stored_definition(psr);

  if (ModelicaParser_lexerError || pLexer->rec->state->failed || psr->pParser->rec->state->failed) { // Some parts of the AST are NULL if errors are used...
    res = jl_nothing;
  }

  for (int i = 0; i < psr->pModelicaParser_omcTop->numPushed; i++) {
    JL_GC_POP();
  }

  psr->free(psr);
  psr = (pModelicaParser) NULL;
  tstream->free(tstream);
  tstream = (pANTLR3_COMMON_TOKEN_STREAM) NULL;
  ((pModelica_3_Lexer)lxr)->free((pModelica_3_Lexer)lxr);
  lxr = NULL;
  input->close(input);
  input = (pANTLR3_INPUT_STREAM) NULL;

  jl_gc_enable(1);

  return res;
}

DLLDirection jl_value_t* parseFile(jl_value_t *fileName, int acceptedGrammar)
{
  pANTLR3_UINT8               fName;
  pANTLR3_INPUT_STREAM        input;

  parser_members members;

  int flags = PARSE_MODELICA;
  if(acceptedGrammar == 2) flags |= PARSE_META_MODELICA;
  else if(acceptedGrammar == 3) flags |= PARSE_PARMODELICA;
  else if(acceptedGrammar == 4) flags |= PARSE_OPTIMICA;
  else if(acceptedGrammar == 5) flags |= PARSE_PDEMODELICA;

  pthread_once(&parser_once_create_key,make_key);
  pthread_setspecific(modelicaParserKey,&members);


  jl_gc_enable(1);

  members.encoding = "UTF-8";
  members.filename_C = fileName;
  members.filename_C_testsuiteFriendly = fileName;
  members.filename_OMC = fileName;
  members.flags = flags;
  members.readonly = 1;
  omc_first_comment = 0;

  fName  = (pANTLR3_UINT8)jl_string_data(fileName);
  input  = antlr3AsciiFileStreamNew(fName);
  if ( input == NULL ) {
    return jl_nothing;
  }
  return parseStream(input);
}
