#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"
#include "record.h"

// Return the correct error (-1 or NULL), if necessary
#define CALL_BF(call, error)  \
{                             \
	BF_ErrorCode code = call; \
	if (code != BF_OK) {      \
		BF_PrintError(code);  \
		return error;         \
	}                         \
}

// Create a file that doesn't already exists
int HP_CreateFile(char *fileName) {
	int fileDesc;
	char *block_data, *message;
	BF_Block *first_block;
	HP_info file_info;

	BF_Block_Init(&first_block); // Initialize a temp block
	CALL_BF(BF_CreateFile(fileName), -1);
	CALL_BF(BF_OpenFile(fileName, &fileDesc), -1);
	CALL_BF(BF_AllocateBlock(fileDesc, first_block), -1);

	// Save file info on the first block
	block_data = BF_Block_GetData(first_block);
	message = "heap"; // Code word to check if it is a heap file
	memcpy(file_info.type, message, strlen(message) + 1);
	file_info.fileDesc = fileDesc;
	file_info.index_last_block = 0;
	file_info.max_records = BF_BLOCK_SIZE/sizeof(Record);
	file_info.recs_last_block = 0;
	file_info.first_block = NULL; // The file doesn't need to this pointer
	memcpy(block_data, &file_info, sizeof(HP_info)); // Insert the data to the first block
	BF_Block_SetDirty(first_block); // We made changes

	CALL_BF(BF_UnpinBlock(first_block), -1); // Write the changes to the disk
	BF_Block_Destroy(&first_block); // Free memory
	CALL_BF(BF_CloseFile(fileDesc), -1);

	return 0;
}

// Open file and extract information
HP_info* HP_OpenFile(char *fileName) {
	HP_info *file_info, *temp;
	char *data;
	int file_desc;

	// Create HP_info that will be used by the program
	file_info = malloc(sizeof(HP_info));
	if (!file_info) {
		printf("Cannot allocate memory!\n");
		return NULL;
	}

	// The pointer (file_info->first_block) points to the first block
	BF_Block_Init(&(file_info->first_block)); // Allocate memory
	CALL_BF(BF_OpenFile(fileName, &file_desc), NULL);
	CALL_BF(BF_GetBlock(file_desc, 0, file_info->first_block), NULL); // Get the first block

	// Check if it is a heap file
	data = BF_Block_GetData(file_info->first_block); // Get its data
	temp = (HP_info*)data;
	if (strcmp(temp->type, "heap") != 0) {
		printf("Error! Not a heap file!\n");
		return NULL;
	}

	// Take the file info from the first block
	file_info->fileDesc = temp->fileDesc;
	file_info->max_records = temp->max_records;
	file_info->index_last_block = temp->index_last_block;
	file_info->recs_last_block = temp->recs_last_block;

	return file_info;
}

// Close file and free memory
int HP_CloseFile(HP_info* hp_info) {
	CALL_BF(BF_UnpinBlock(hp_info->first_block), -1); // Unpin the first block
	BF_Block_Destroy(&(hp_info->first_block)); // Destroy the first block
	CALL_BF(BF_CloseFile(hp_info->fileDesc), -1); // Close the heap file
	free(hp_info); // Free memory
	return 0;
}

// Insert one entry in the last block of the file
int HP_InsertEntry(HP_info* hp_info, Record record) {
	char* data;
	BF_Block *temp_block;
	HP_info *temp_ptr;

	BF_Block_Init(&temp_block); // Allocate memory for a temp block

	// This is the first block with the special info
	if (hp_info->index_last_block == 0) {
		CALL_BF(BF_AllocateBlock(hp_info->fileDesc, temp_block), -1);
		hp_info->index_last_block++;
	}
	// The last block can't take another record, create a new block
	else if (hp_info->recs_last_block == hp_info->max_records) {
		CALL_BF(BF_AllocateBlock(hp_info->fileDesc, temp_block), -1);
		hp_info->index_last_block++;
		hp_info->recs_last_block = 0;
	}
	// Insert the record in the last block
	else {
		CALL_BF(BF_GetBlock(hp_info->fileDesc, hp_info->index_last_block, temp_block), -1);
	}
	data = BF_Block_GetData(temp_block);

	int index = hp_info->recs_last_block * sizeof(Record); // Where the next entry will be placed, inside the block.
	memcpy(data+index, &record, sizeof(Record)); // Change the block's data
	BF_Block_SetDirty(temp_block); // SetDirty since we changed the data
	CALL_BF(BF_UnpinBlock(temp_block), -1);
	BF_Block_Destroy(&temp_block); // Free memory
	hp_info->recs_last_block++; // The last block has one more record now

	// Update file info on the first block
	data = BF_Block_GetData(hp_info->first_block); // Get its data
	temp_ptr = (HP_info*)data;
	memcpy(&(temp_ptr->index_last_block), &(hp_info->index_last_block), sizeof(int));
	memcpy(&(temp_ptr->recs_last_block), &(hp_info->recs_last_block), sizeof(int));
	BF_Block_SetDirty(hp_info->first_block);
	
	// Return the ID of the last block
	return hp_info->index_last_block;
}

// Print the entry with id==value
int HP_GetAllEntries(HP_info* hp_info, int value) {
	int i, j, index, count, found=0;
	char *data, *rec_ptr;
	BF_Block *temp_block;
	Record *record;

	// Search every record exept the first and the last one
	BF_Block_Init(&temp_block);
	for (i = 1; i < hp_info->index_last_block; i++) {
		CALL_BF(BF_GetBlock(hp_info->fileDesc, i, temp_block), -1);
		data = BF_Block_GetData(temp_block);
		for (j = 0; j < hp_info->max_records; j++) {
			index = j * sizeof(Record);
			rec_ptr = data + index;
			record = (Record*)rec_ptr;
			if (record->id == value) { // Record found
				found = i; // Flag
				printRecord(*record);
				break;
			}
		}
		CALL_BF(BF_UnpinBlock(temp_block), -1);
		// We found the record, stop searching
		if (found != 0 ) {
			BF_Block_Destroy(&temp_block); // Free memory
			return found; // Return the number of blocks we searched
		}
	}
	// Search the last block
	CALL_BF(BF_GetBlock(hp_info->fileDesc, hp_info->index_last_block, temp_block), -1);
	data = BF_Block_GetData(temp_block);
	for (j = 0; j < hp_info->recs_last_block; j++) {
		index = j * sizeof(Record);
		rec_ptr = data + index;
		record = (Record*)rec_ptr;
		if (record->id == value) { // Record found
			found = hp_info->index_last_block;
			printRecord(*record);
			break; // Stop searching
		}
	}
	CALL_BF(BF_UnpinBlock(temp_block), -1); // We don't need the temp block anymore
	BF_Block_Destroy(&temp_block); // Free memory

	// Return the number of blocks we searched
	return found;
}