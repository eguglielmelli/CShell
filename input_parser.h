#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

typedef struct input_parser
{
	char* str;
	char* position;

}INPUT_PARSER;

char* remove_whitespace(char input[]);

INPUT_PARSER* init_input_parser(char* string);

char* get_token(INPUT_PARSER* parser);

void free_input_parser(INPUT_PARSER* parser);

void free_tokens(char* tokens[],int start_index);

#endif 
