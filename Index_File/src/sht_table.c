#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "sht_table.h"
#include "ht_table.h"
#include "record.h"

#define CALL_SHT(call)        \
{                             \
	BF_ErrorCode code = call; \
	if (code != BF_OK) {      \
		BF_PrintError(code);  \
		return -1;            \
	}                         \
}

typedef struct SHT_Record {
	char name[15];
	int block_id;
	int record_id;
} SHT_Record;

BF_ErrorCode SHT_AllocateBlock(int file_desc, BF_Block *block);

int hash_function(char *name, int num_of_buckets);

int SHT_CreateSecondaryIndex(char *sfileName, int buckets, char *fileName){
	int file_desc;
	BF_Block *block;
	int *info;
	char *data;

	BF_Block_Init(&block);

	/* Create the file */
	CALL_SHT(BF_CreateFile(sfileName));

	/* Open the file */
	CALL_SHT(BF_OpenFile(sfileName, &file_desc));
	CALL_SHT(BF_AllocateBlock(file_desc, block));

	/* Write the initializing data for the file */
	data = BF_Block_GetData(block);
	sprintf(data, "hash");
	info = (int *)(data + 5);
	info[0] = buckets;
	for (int i = 1; i <= buckets; i++) {
		info[i] = i;
	}

	/* Unpin the block */
	BF_Block_SetDirty(block);
	CALL_SHT(BF_UnpinBlock(block));

	/* Create the first buckets */
	for (int i = 0; i < buckets; i++) {
		CALL_SHT(SHT_AllocateBlock(file_desc, block));
		CALL_SHT(BF_UnpinBlock(block));
	}

	CALL_SHT(BF_CloseFile(file_desc));
	BF_Block_Destroy(&block);

	return 0;
}

SHT_info* SHT_OpenSecondaryIndex(char *sfileName){
	BF_ErrorCode code;
	int file_desc;
	int *info;
	char *data;

	/* Allocate the memory for SHT_info */
	SHT_info *sht_info = malloc(sizeof(HT_info));
	BF_Block_Init(&sht_info->block);

	code = BF_OpenFile(sfileName, &file_desc);
	if (code != BF_OK) { 
		BF_PrintError(code);
		return NULL;
	}

	/* Load the first block */
	code = BF_GetBlock(file_desc, 0, sht_info->block);
	if (code != BF_OK) { 
		BF_PrintError(code);
		return NULL;
	}

	/*  Get the ht_info */
	data = BF_Block_GetData(sht_info->block);
	if (strcmp(data, "hash") != 0) {
		return NULL;
	}
	info = (int *)(data + 5);
	sht_info->file_desc = file_desc;
	sht_info->num_of_buckets = info[0];
	sht_info->insert_block_ids = malloc(sht_info->num_of_buckets * sizeof(int));
	for (int i = 0; i < sht_info->num_of_buckets; i++) {
		sht_info->insert_block_ids[i] = info[i+1];
	}

	/* Return the ht_info */
	return sht_info;
}

int SHT_CloseSecondaryIndex(SHT_info *header_info){
	/* Unpin the first block */
	CALL_SHT(BF_UnpinBlock(header_info->block));
	BF_Block_Destroy(&header_info->block);

	/* Close the file */
	CALL_SHT(BF_CloseFile(header_info->file_desc));

	/* Free the memory used for sht_info */
	free(header_info->insert_block_ids);
	free(header_info);

	return 0;
}

int SHT_SecondaryInsertEntry(SHT_info *sht_info, Record record, int block_id) {
	int bucket = hash_function(record.name, sht_info->num_of_buckets);
	int sht_block_id = sht_info->insert_block_ids[bucket];
	int *data;
	SHT_block_info *block_info;
	SHT_Record *records;
	SHT_Record sht_record;
	BF_Block *block;

	sht_record.block_id = block_id;
	sht_record.record_id = record.id;
	strcpy(sht_record.name, record.name);

	BF_Block_Init(&block);
	CALL_SHT(BF_GetBlock(sht_info->file_desc, sht_block_id, block));
	block_info = (SHT_block_info *)(BF_Block_GetData(block));

	/* If there's enough space for the record to be inserted */
	if (block_info->rec_cnt != ((BF_BLOCK_SIZE - sizeof(SHT_block_info))/sizeof(SHT_Record))) {
		/* Insert the record to the block */
		records = (SHT_Record *)(block_info + 1);
		memcpy(&records[block_info->rec_cnt], &sht_record, sizeof(SHT_Record));
		block_info->rec_cnt++;
		BF_Block_SetDirty(block);
		CALL_SHT(BF_UnpinBlock(block));
	}
	else {
		/* Make the last block point to the block that will be created */
		CALL_SHT(BF_GetBlockCounter(sht_info->file_desc, &block_info->next_block));
		sht_info->insert_block_ids[bucket] = block_info->next_block;
		BF_Block_SetDirty(block);
		CALL_SHT(BF_UnpinBlock(block));

		/* Update the first block about the change */
		data = (int *)(BF_Block_GetData(sht_info->block) + 5);
		data[bucket + 1] = block_info->next_block;
		BF_Block_SetDirty(sht_info->block);

		/* Create a new block and insert the record there */
		CALL_SHT(SHT_AllocateBlock(sht_info->file_desc, block));
		block_info = (SHT_block_info *)(BF_Block_GetData(block));
		records = (SHT_Record *)(block_info + 1);
		memcpy(records, &sht_record, sizeof(SHT_Record));
		block_info->rec_cnt = 1;

		BF_Block_SetDirty(block);
		CALL_SHT(BF_UnpinBlock(block));
	}
	BF_Block_Destroy(&block);

	return 0;
}

int SHT_SecondaryGetAllEntries(HT_info *ht_info, SHT_info *sht_info, char *name) {
	int block_cnt = 0;
	int bucket = hash_function(name, sht_info->num_of_buckets);
	int block_id = bucket + 1;
	SHT_block_info *block_info;
	HT_block_info *ht_block_info;
	SHT_Record *records;
	Record *ht_records;
	BF_Block *block, *ht_block;

	BF_Block_Init(&block);
	BF_Block_Init(&ht_block);

	/* While there are more blocks */
	while (block_id != -1) {
		block_cnt++;

		/* Load the block */
		CALL_SHT(BF_GetBlock(sht_info->file_desc, block_id, block));
		block_info = (SHT_block_info *)(BF_Block_GetData(block));
		records = (SHT_Record *)(block_info + 1);

		/* Check if the record is inside the block */
		for (int i = 0; i < block_info->rec_cnt; i++) {
			if (strcmp(records[i].name, name) == 0) {
				/* A record with the correct name is found */
				CALL_SHT(BF_GetBlock(ht_info->file_desc, records[i].block_id, ht_block));
				ht_block_info = (HT_block_info *)(BF_Block_GetData(ht_block));
				ht_records = (Record *)(ht_block_info + 1);
				for (int j = 0; j < ht_block_info->rec_cnt; j++) {
					if (ht_records[j].id == records[i].record_id) {
						printRecord(ht_records[j]);
					}
				}
				CALL_SHT(BF_UnpinBlock(ht_block));
			}
		}
		block_id = block_info->next_block;
		CALL_SHT(BF_UnpinBlock(block));
	}
	/* The record is not found */
	BF_Block_Destroy(&block);
	BF_Block_Destroy(&ht_block);

	return block_cnt;
}

BF_ErrorCode SHT_AllocateBlock(int file_desc, BF_Block *block) {
	SHT_block_info *info;
	BF_ErrorCode code;

	/* Create a new block */
	code = BF_AllocateBlock(file_desc, block);
	if (code != BF_OK) {
		return code;
	}

	/* Initialize the information about the block */
	info = (SHT_block_info *)(BF_Block_GetData(block));
	info->next_block = -1;
	info->rec_cnt = 0;
	
	BF_Block_SetDirty(block);

	return BF_OK;
}

int hash_function(char *name, int num_of_buckets) {
	int ret = 0, i = 0;
	while (name[i] != '\0') {
		ret = (ret + name[i]) % num_of_buckets;
		i++;
	}
	return ret;
}