#include "input_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


/**
 * Removes the front and end whitespace from an input
 * @param input of characters that will be passed from STDIN
 * @return a trimmed pointer
**/
char* remove_whitespace(char input[])
{
	int start = 0;

	int end = strlen(input)-1;


	//trim whitespace on the front end
	while(isspace((unsigned char) input[start]) && input[start] != '\0')
	{
	        start++;
	}

	//trim whitespace on the back end
	while(end > start && isspace((unsigned char) input[end]))
	{
	        end--;
	}

	int trimmed_len = end - start + 1;


	//set up our pointer to be return 

    char* new_input = malloc(trimmed_len+1);

    if(new_input == NULL)
    {
        perror("malloc error");
        exit(EXIT_FAILURE);
    }

    memcpy(new_input,&input[start],trimmed_len);

    new_input[trimmed_len] = '\0';


    return new_input;

}

/**
 * Initializes a new input parser to be used to get tokens
 * @param input that the parser will use
 * @return an initialized parser, NULL if error
**/
INPUT_PARSER* init_input_parser(char* input)
{
	if(input == NULL)
	{
		fprintf(stderr,"Cannot initialize input parser. Input is NULL");
		return NULL;
	}

	//initialize our parser, check for malloc error
	INPUT_PARSER* input_parser = malloc(sizeof(INPUT_PARSER));

	if(input_parser == NULL)
	{
		perror("Could not allocate memory for parser");
		return NULL;
	}


	int length = strlen(input)+1;

	input_parser->str = (char*) malloc(length);

	//copy over our input to struct attribute 
	//and set position
	memcpy(input_parser->str,input,length);

	input_parser->position = input_parser->str;

	printf("%s\n","Parser Initialized!");

	return input_parser;
}

/**
 * Gets the next token from the input parser
 * @param initialized input parser
 * @return parsed tokens, NULL if string is empty 
 * or input parser is NULL
**/
char* get_token(INPUT_PARSER* parser)
{
	if(parser == NULL)
	{
		perror("Parser that is passed in must be initialized");
		return NULL;
	}

	char *start = parser->position;

	// make sure we're not at the end
    if (*start == '\0') 
    { 
        return NULL;
    }

    parser->position = start;

    char *end = start;

    // Check for delimiters and handle them as separate tokens
    if (strchr("|&<>", *end) != NULL) 
    {
        end++;

        //handle two delimiters in a row such as >>
        if (*end == *start) 
        { 
            end++;
        }
    } 
    else 
    {
        // Move to the next delimiter or whitespace
        while (*end && !isspace((unsigned char)*end) && !strchr("|&<>", *end)) 
        {
            end++;
        }
    }

    int len = end - start;

    char *token = malloc(len + 1);

    //null check for malloc error
    if (token == NULL) 
    {
        perror("Failed to allocate memory for token");
        return NULL;
    }

    //copy over our new token, null terminate, then set parser position
    strncpy(token, start, len);
    token[len] = '\0'; 

    parser->position = end; 

 	//skip over any white space for the next call
    while (isspace((unsigned char)*parser->position)) 
    {
        parser->position++;
    }

    return token;

}

void free_input_parser(INPUT_PARSER* parser)
{
	if(parser == NULL)
	{
		fprintf(stderr,"Need a non-null parser to deallocate \n");
		return;
	}

	free(parser->str);
	free(parser);

	return;
}