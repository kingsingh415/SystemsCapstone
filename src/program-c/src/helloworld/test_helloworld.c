#include "helloworld.c"
#include <criterion/criterion.h>

Test(hello, sanity) {
  uint8_t instruction_data[] = { 'P', 't', 'e', 's', 't'};
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  uint64_t lamports = 1;
  uint8_t data[1024] = {0};
  SolAccountInfo accounts[] = {{
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
  }};
  SolParameters params = {accounts, sizeof(accounts), instruction_data,
                          sizeof(instruction_data), &program_id};

  // Check offset calculation on blank account
  cr_assert(sizeof(AccountMetadata) == newDataOffset(data, sizeof(data)));

  // Check posting and offset calculation
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + sizeof(instruction_data) + sizeof(uint16_t) 
            == newDataOffset(data, sizeof(data)));
  Post p;
  cr_assert(sizeof(uint16_t) + sizeof(instruction_data) == parsePost(instruction_data, sizeof(instruction_data), &p));
  AccountMetadata* d = (AccountMetadata*)data;
  cr_assert(1 == d->numPosts);
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + (sizeof(instruction_data) + sizeof(uint16_t)) * 2
            == newDataOffset(data, sizeof(data)));
  cr_assert(2 == d->numPosts);
}

Test(hello, reply) {
  uint8_t instruction_data[1 + sizeof(PostID) + 5] = { 0 };
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  // Setup reply
  PostID replyTo = { .poster = key, .index = 0 };
  char t = 'R';
  sol_memcpy(instruction_data, &t, 1);
  sol_memcpy(&instruction_data[1], &replyTo, sizeof(PostID));
  sol_memcpy(&instruction_data[1 + sizeof(PostID)], "Reply", 5);

  // Setup account data
  uint64_t lamports = 1;
  uint8_t data[51] = {0};
  AccountMetadata* meta = (AccountMetadata*)data;
  meta->numPosts = 1;
  uint16_t firstPostLength = 5;
  sol_memcpy(data + sizeof(AccountMetadata), &firstPostLength, sizeof(uint16_t));
  sol_memcpy(data + sizeof(AccountMetadata) + sizeof(uint16_t), "Ptest", firstPostLength);
  SolAccountInfo accounts[] = {{
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
  }};
  SolParameters params = {accounts, sizeof(accounts), instruction_data,
                          sizeof(instruction_data), &program_id};
  cr_assert(SUCCESS == helloworld(&params));
  //sol_log("Account data after successful transaction:");
  //sol_log_array(data, sizeof(data));
}

Test(hello, like) {
  uint8_t instruction_data[1 + sizeof(PostID)] = { 0 };
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  // Setup like
  PostID replyTo = { .poster = key, .index = 0 };
  instruction_data[0] = 'L';
  sol_memcpy(&instruction_data[1], &replyTo, sizeof(PostID));

  // Setup account data
  uint64_t lamports = 1;
  uint8_t data[51] = {0};
  AccountMetadata* meta = (AccountMetadata*)data;
  meta->numPosts = 1;
  uint16_t firstPostLength = 5;
  sol_memcpy(data + sizeof(AccountMetadata), &firstPostLength, sizeof(uint16_t));
  sol_memcpy(data + sizeof(AccountMetadata) + sizeof(uint16_t), "Ptest", firstPostLength);
  SolAccountInfo accounts[] = {{
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
  }};
  SolParameters params = {accounts, sizeof(accounts), instruction_data,
                          sizeof(instruction_data), &program_id};
  cr_assert(SUCCESS == helloworld(&params));
}