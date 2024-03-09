#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "bf.h"
#include "ht_table.h"

#define RECORDS_NUM 200 // you can change it if you want
#define FILE_NAME "data.db"

int main(void) {
	BF_Init(LRU);
	HT_CreateFile(FILE_NAME, 10);
	HT_info* info = HT_OpenFile(FILE_NAME);

	Record record;
	srand(12569874);
	printf("Insert Entries\n");
	for (int id = 0; id < RECORDS_NUM; ++id) {
		record = randomRecord();
		HT_InsertEntry(info, record);
	}

	printf("RUN PrintAllEntries\n");
	int id = rand() % RECORDS_NUM;
	printf("\nSearching for: %d\n", id);
	HT_GetAllEntries(info, id);

	HT_CloseFile(info);
	HashStatistics(FILE_NAME);
	BF_Close();

	return 0;
}