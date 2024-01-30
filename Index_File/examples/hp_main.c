#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "bf.h"
#include "hp_file.h"

#define RECORDS_NUM 1000 // You can change it if you want
#define FILE_NAME "data.db"

int main(void) {
	int r, id, total;
	Record record;

	srand(time(NULL)); // Every time we run the program we have a different seed for srand
	BF_Init(LRU);
	HP_CreateFile(FILE_NAME);
	HP_info* info = HP_OpenFile(FILE_NAME);
	
	printf("Insert Entries\n");
	for (r = 0; r < RECORDS_NUM; ++r) {
		record = randomRecord();
		HP_InsertEntry(info, record);
	}

	printf("RUN PrintAllEntries\n");
	id = rand() % RECORDS_NUM;
	printf("\nSearching for: %d\n", id);
	total = HP_GetAllEntries(info, id);
	printf("Searched %d blocks\n", total); // Print the number of blocks we searched in order to find the record
	HP_CloseFile(info);
	BF_Close();
	
	return 0;
}