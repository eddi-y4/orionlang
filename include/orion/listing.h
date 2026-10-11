#ifndef ORION_LISTING_H
#define ORION_LISTING_H

#include "ast.h"

int write_bytecode_listing(ASTNode *program, const char *path);
char *generate_bytecode_listing(ASTNode *program);

#endif
