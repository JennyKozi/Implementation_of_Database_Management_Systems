#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "ht_table.h"
#include "record.h"

#define CALL_HT(call)         \
{                             \
	BF_ErrorCode code = call; \
	if (code != BF_OK) {      \
		BF_PrintError(code);  \
		return -1;            \
	}                         \
}

/* Helper function that creates a new BF_block and initializes its information */
BF_ErrorCode HT_AllocateBlock(int file_desc, BF_Block *block);

int HT_CreateFile(char *fileName, int buckets) {
	int file_desc;
	BF_Block *block;
	int *info;
	char *data;

	BF_Block_Init(&block);

	/* Create the file */
	CALL_HT(BF_CreateFile(fileName));

	/* Open the file */
	CALL_HT(BF_OpenFile(fileName, &file_desc));
	CALL_HT(BF_AllocateBlock(file_desc, block));

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
	CALL_HT(BF_UnpinBlock(block));

	/* Create the first buckets */
	for (int i = 0; i < buckets; i++) {
		CALL_HT(HT_AllocateBlock(file_desc, block));
		CALL_HT(BF_UnpinBlock(block));
	}

	CALL_HT(BF_CloseFile(file_desc));
	BF_Block_Destroy(&block);

	return 0;
}

HT_info* HT_OpenFile(char *fileName){
	BF_ErrorCode code;
	int file_desc;
	int *info;
	char *data;

	/* Allocate the memory for HT_info */
	HT_info *ht_info = malloc(sizeof(HT_info));
	BF_Block_Init(&ht_info->block);

	code = BF_OpenFile(fileName, &file_desc);
	if (code != BF_OK) { 
		BF_PrintError(code);
		return NULL;
	}

	/* Load the first block */
	code = BF_GetBlock(file_desc, 0, ht_info->block);
	if (code != BF_OK) { 
		BF_PrintError(code);
		return NULL;
	}

	/*  Get the ht_info */
	data = BF_Block_GetData(ht_info->block);
	if (strcmp(data, "hash") != 0) {
  		return NULL;
 	}
	info = (int *)(data + 5);
	ht_info->file_desc = file_desc;
	ht_info->num_of_buckets = info[0];
	ht_info->insert_block_ids = malloc(ht_info->num_of_buckets * sizeof(int));
	for (int i = 0; i < ht_info->num_of_buckets; i++) {
  		ht_info->insert_block_ids[i] = info[i+1];
	}

	/* Return the ht_info */
	return ht_info;
}

int HT_CloseFile(HT_info* ht_info){
	/* Unpin the first block */
	CALL_HT(BF_UnpinBlock(ht_info->block));
	BF_Block_Destroy(&ht_info->block);

	/* Close the file */
	CALL_HT(BF_CloseFile(ht_info->file_desc));

	/* Free the memory used for ht_info */
	free(ht_info->insert_block_ids);
	free(ht_info);

	return 0;
}

int HT_InsertEntry(HT_info* ht_info, Record record){
	int bucket = record.id % ht_info->num_of_buckets; 
	int block_id = ht_info->insert_block_ids[bucket];
	int *data;
	HT_block_info *block_info;
	Record *records;
	BF_Block *block;

	BF_Block_Init(&block);
	CALL_HT(BF_GetBlock(ht_info->file_desc, block_id, block));
	block_info = (HT_block_info *)(BF_Block_GetData(block));

	/* If there's enough space for the record to be inserted */
	if (block_info->rec_cnt != ((BF_BLOCK_SIZE - sizeof(HT_block_info))/sizeof(Record))) {
		/* Insert the record to the block */
		records = (Record *)(block_info + 1);
		memcpy(records + block_info->rec_cnt, &record, sizeof(Record));
		block_info->rec_cnt++;
		BF_Block_SetDirty(block);
		CALL_HT(BF_UnpinBlock(block));
	}
	else {
		/* Make the last block point to the block that will be created */
		CALL_HT(BF_GetBlockCounter(ht_info->file_desc, &block_info->next_block));
		ht_info->insert_block_ids[bucket] = block_info->next_block;
		BF_Block_SetDirty(block);
		CALL_HT(BF_UnpinBlock(block));

		/* Update the first block about the change */
		data = (int *)(BF_Block_GetData(ht_info->block) + 5);
		data[bucket + 1] = block_info->next_block;
		BF_Block_SetDirty(ht_info->block);

		/* Create a new block and insert the record there */
		CALL_HT(HT_AllocateBlock(ht_info->file_desc, block));
		block_info = (HT_block_info *)(BF_Block_GetData(block));
		records = (Record *)(block_info + 1);
		memcpy(records, &record, sizeof(Record));
		block_info->rec_cnt = 1;

		BF_Block_SetDirty(block);
		CALL_HT(BF_UnpinBlock(block));
	}
	BF_Block_Destroy(&block);

	return ht_info->insert_block_ids[bucket];
}

int HT_GetAllEntries(HT_info* ht_info, int value){
	int block_cnt = 0;
	int bucket = value % ht_info->num_of_buckets; 
	int block_id = bucket + 1;
	HT_block_info *block_info;
	Record *records;
	BF_Block *block;
	
	BF_Block_Init(&block);

	/* While there are more blocks */
	while (block_id != -1) {
		block_cnt++;

		/* Load the block */
		CALL_HT(BF_GetBlock(ht_info->file_desc, block_id, block));
		block_info = (HT_block_info *)(BF_Block_GetData(block));
		records = (Record *)(block_info + 1);
		
		/* Check if the record is inside the block */
		for (int i = 0; i < block_info->rec_cnt; i++) {
			if (records[i].id == value) {
  				/* The record is found */
				printRecord(records[i]);
				CALL_HT(BF_UnpinBlock(block));
				BF_Block_Destroy(&block);
				return block_cnt;
			}
		}
		block_id = block_info->next_block;
		CALL_HT(BF_UnpinBlock(block));
	}
	/* The record is not found */
	BF_Block_Destroy(&block);

	return -1;
}

BF_ErrorCode HT_AllocateBlock(int file_desc, BF_Block *block) {
	HT_block_info *info;
	BF_ErrorCode code;

	/* Create a new block */
	code = BF_AllocateBlock(file_desc, block);
	if (code != BF_OK) {
		return code;
	}

	/* Initialize the information about the block */
	info = (HT_block_info *)(BF_Block_GetData(block));
	info->next_block = -1;
	info->rec_cnt = 0;
	
	BF_Block_SetDirty(block);

	return BF_OK;
}

int HashStatistics(char* filename) {
	int block_cnt = 1;
	int bucket_records, total_records = 0, min_records = -1, max_records = -1;
	int blocks_per_bucket_cnt, overflowed_buckets = 0; 
	int block_id;
	HT_block_info *block_info;
	BF_Block *block;

	HT_info *ht_info = HT_OpenFile(filename);
	if (ht_info == NULL) {
		return -1;
	}
	printf("\nPrinting statistics of the file %s\n", filename);
	BF_Block_Init(&block);

	/* For every bucket */
	for (int i = 0; i < ht_info->num_of_buckets; i++) {
		block_id = i + 1;
		blocks_per_bucket_cnt = 0;
		bucket_records = 0;
		while (block_id != -1) {
			block_cnt++;
			blocks_per_bucket_cnt++;
			CALL_HT(BF_GetBlock(ht_info->file_desc, block_id, block));
			block_info = (HT_block_info *)(BF_Block_GetData(block));
			bucket_records += block_info->rec_cnt;
			block_id = block_info->next_block;
			CALL_HT(BF_UnpinBlock(block));
		}
		total_records += bucket_records;

		/* Measure the statistics of the bucket */
		if (min_records == -1 || bucket_records < min_records) {
			min_records = bucket_records;
		}
		if (bucket_records > max_records) {
			max_records = bucket_records;
		}
		if (blocks_per_bucket_cnt > 1) {
			printf("Bucket no. %d has %d overflow blocks\n", i, blocks_per_bucket_cnt - 1);
			overflowed_buckets++;
		}
	}
	/* Print the statistics */
	printf("Total blocks: %d\n", block_cnt);
	printf("Average number of records: %f\n", ((double)(total_records))/ht_info->num_of_buckets);
	printf("Minimum number of records inside a bucket: %d\n", min_records);
	printf("Maximum number of records inside a bucket: %d\n", max_records);
	printf("Average number of blocks: %f\n", ((double)(block_cnt + 1))/ht_info->num_of_buckets);
	printf("Number of buckets that have overflow blocks: %d\n", overflowed_buckets);

	BF_Block_Destroy(&block);

	return HT_CloseFile(ht_info);
}