#include <stdlib.h>
#include <stdio.h>
#include <time.h>

char allow_characters[] = "abcdrhjvroijiojrootrioonhbibiewbdbibidiybibibibibwixu122333323234 \" $ \\ % & /()=";


int main()
{

srand(time(NULL));
char password[20];


for(int i =0; i < sizeof(password)-1; i++)
{

   int random_number = rand() % (sizeof(allow_characters)-1); 
   password[i] = allow_characters[random_number];



}
    password[19] = 0; 
    printf("your password is :%s\n" , password);

}
