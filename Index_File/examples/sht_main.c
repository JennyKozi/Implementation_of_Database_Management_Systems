#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bf.h"
#include "ht_table.h"
#include "sht_table.h"

#define RECORDS_NUM 200
#define FILE_NAME "data.db"
#define INDEX_NAME "index.db"

int main(void) {
	int i, block_id;

	BF_Init(LRU);
	srand(time(NULL));
	Record record=randomRecord();
	char searchName[15];
	strcpy(searchName, record.name);
	
	// Create files
	HT_CreateFile(FILE_NAME, 10);
	SHT_CreateSecondaryIndex(INDEX_NAME, 10, FILE_NAME);

	// Open files
	HT_info* info = HT_OpenFile(FILE_NAME);
	SHT_info* index_info = SHT_OpenSecondaryIndex(INDEX_NAME);

	// Insert random records to the files
	printf("Insert Records\n");
	for (i = 0; i < RECORDS_NUM; i++) {
		record = randomRecord();
		block_id = HT_InsertEntry(info, record);
		SHT_SecondaryInsertEntry(index_info, record, block_id);
	}
	// Print all records with name=searchName
	printf("\nPrint all records with name %s\n", searchName);
	SHT_SecondaryGetAllEntries(info, index_info, searchName);

	// Close files
	SHT_CloseSecondaryIndex(index_info);
	HT_CloseFile(info);

	// Hash statistics for sht
	HashStatistics(INDEX_NAME);
	BF_Close();

	return 0;
}