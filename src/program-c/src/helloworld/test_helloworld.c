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
  uint8_t data[128] = {0};
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
  SolParameters params = {accounts, sizeof(accounts) / sizeof(accounts[0]), instruction_data,
                          sizeof(instruction_data), &program_id};

  // Check offset calculation on blank account
  cr_assert(sizeof(AccountMetadata) == newPostOffset(data, sizeof(data)));

  // Check posting and offset calculation
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + sizeof(instruction_data) + sizeof(uint16_t) 
            == newPostOffset(data, sizeof(data)));
  Post p;
  cr_assert(sizeof(uint16_t) + sizeof(instruction_data) == parsePost(instruction_data, sizeof(instruction_data), &p));
  AccountMetadata* d = (AccountMetadata*)data;
  cr_assert(1 == d->numPosts);
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + (sizeof(instruction_data) + sizeof(uint16_t)) * 2
            == newPostOffset(data, sizeof(data)));
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
  uint8_t data[100] = {0};
  AccountMetadata* meta = (AccountMetadata*)data;
  initializeUserAccount(data, sizeof(data));
  uint16_t firstPostLength = 5;
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
  SolParameters postParams = {accounts, sizeof(accounts) / sizeof(accounts[0]), (unsigned char*)"Ptest",
                          firstPostLength, &program_id};
  cr_assert(SUCCESS == helloworld(&postParams));
  cr_assert(meta->numPosts == 1);
  cr_assert(meta->accountType == User);
  SolParameters params = {accounts, sizeof(accounts) / sizeof(accounts[0]), instruction_data,
                          sizeof(instruction_data), &program_id};
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(meta->numPosts == 2);
  sol_log_array(data, sizeof(data));
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
  uint8_t data[128] = {0};
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
  SolParameters params = {accounts, sizeof(accounts) / sizeof(accounts[0]), instruction_data,
                          sizeof(instruction_data), &program_id};
  cr_assert(SUCCESS == helloworld(&params));
}

Test(hello, petitionVoteFail) {
  // Make a post to petition against
  uint8_t instruction_data[] = { 'P', 't', 'e', 's', 't'};
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  uint64_t lamports = 1;
  uint8_t data[128] = {0};
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
  SolParameters params = {accounts, sizeof(accounts) / sizeof(accounts[0]), instruction_data,
                          sizeof(instruction_data), &program_id};

  // Check offset calculation on blank account
  cr_assert(sizeof(AccountMetadata) == newPostOffset(data, sizeof(data)));

  // Check posting and offset calculation
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + sizeof(instruction_data) + sizeof(uint16_t) 
            == newPostOffset(data, sizeof(data)));
  Post p;
  cr_assert(sizeof(uint16_t) + sizeof(instruction_data) == parsePost(instruction_data, sizeof(instruction_data), &p));
  AccountMetadata* d = (AccountMetadata*)data;
  d->reputation = 5;
  cr_assert(1 == d->numPosts);
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + (sizeof(instruction_data) + sizeof(uint16_t)) * 2
            == newPostOffset(data, sizeof(data)));
  cr_assert(2 == d->numPosts);

  // Create a petition account
  uint8_t vote_instruction_data[] = { 'V', 1 };
  SolPubkey petitionKey = { .x = { 3, }};
  PostID offender = { .poster = key, .index = 0 };
  // The petition holds 1 signature
  uint8_t petitionData[sizeof(PetitionAccountMeta) + (1 * sizeof(PetitionSignature))] = { 0 };
  initializePetitionAccount(petitionData, sizeof(petitionData), &offender, data, sizeof(data));
  // Vote on the petition
  SolAccountInfo voteAccounts[] = {
    {
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
    },
    {
      &petitionKey,
      &lamports,
      sizeof(petitionData),
      petitionData,
      &program_id,
      0,
      false,
      true,
      false,
    }
  };
  SolParameters voteParams = {voteAccounts, SOL_ARRAY_SIZE(voteAccounts), vote_instruction_data,
                          sizeof(vote_instruction_data), &program_id};
  cr_assert(SUCCESS != helloworld(&voteParams));
}

Test(hello, petitionVoteSucceed) {
  // Make a post to petition against
  //sol_log_64(1, 2, 4, 8, sizeof(PetitionSignature));
  uint8_t instruction_data[] = { 'P', 't', 'e', 's', 't'};
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  uint64_t lamports = 1;
  uint8_t data[100] = {0};
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
  SolParameters params = {accounts, sizeof(accounts) / sizeof(accounts[0]), instruction_data,
                          sizeof(instruction_data), &program_id};

  // Check offset calculation on blank account
  cr_assert(sizeof(AccountMetadata) == newPostOffset(data, sizeof(data)));

  // Check posting and offset calculation
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + sizeof(instruction_data) + sizeof(uint16_t) 
            == newPostOffset(data, sizeof(data)));
  Post p;
  cr_assert(sizeof(uint16_t) + sizeof(instruction_data) == parsePost(instruction_data, sizeof(instruction_data), &p));
  AccountMetadata* d = (AccountMetadata*)data;
  cr_assert(1 == d->numPosts);
  cr_assert(SUCCESS == helloworld(&params));
  cr_assert(sizeof(AccountMetadata) + (sizeof(instruction_data) + sizeof(uint16_t)) * 2
            == newPostOffset(data, sizeof(data)));
  cr_assert(2 == d->numPosts);

  // Create a petition account
  uint8_t vote_instruction_data[] = { 'V', 1 };
  SolPubkey petitionKey = { .x = { 3, }};
  PostID offender = { .poster = key, .index = 0 };
  // The petition holds 1 signature
  uint8_t petitionData[sizeof(PetitionAccountMeta) + (1 * sizeof(PetitionSignature))] = { 0 };
  initializePetitionAccount(petitionData, sizeof(petitionData), &offender, data, sizeof(data));
  // Vote on the petition
  SolAccountInfo voteAccounts[] = {
    {
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
    },
    {
      &petitionKey,
      &lamports,
      sizeof(petitionData),
      petitionData,
      &program_id,
      0,
      false,
      true,
      false,
    }
  };
  SolParameters voteParams = {voteAccounts, SOL_ARRAY_SIZE(voteAccounts), vote_instruction_data,
                          sizeof(vote_instruction_data), &program_id};
  d->reputation = 1;
  cr_assert(SUCCESS == helloworld(&voteParams));
  cr_assert(d->reputation == 1); // Reputation will be unchanged because the penalty for getting petitioned and the reward for voting correctly are the same
  //sol_log_array(data, sizeof(data));
}

Test(hello, createPetition) {
  uint8_t instruction_data[] = { 'C', 0, 0 };
  SolPubkey program_id = {.x = {
                              1,
                          }};
  SolPubkey key = {.x = {
                       2,
                   }};
  SolPubkey offenderKey = {.x = {
                       3,
                   }};
  uint64_t lamports = 1;
  uint8_t data[128] = {0};
  uint8_t offenderData[128] = {0};
  SolAccountInfo accounts[] = {
      {
      &key,
      &lamports,
      sizeof(data),
      data,
      &program_id,
      0,
      true,
      true,
      false,
    },
    {
      &offenderKey,
      &lamports,
      sizeof(offenderData),
      offenderData,
      &program_id,
      0,
      false,
      true,
      false,
    },
  };
  SolParameters params = {accounts, SOL_ARRAY_SIZE(accounts), instruction_data,
                          sizeof(instruction_data), &program_id};

  cr_assert(SUCCESS == helloworld(&params));
  PetitionAccountMeta* meta = (PetitionAccountMeta*)data;
  cr_assert(meta->accountType == Petition);
  cr_assert(meta->netTally == 0);
  cr_assert(meta->numSignatures == 0);
  cr_assert(meta->offendingPost.index == 0);
  cr_assert(SolPubkey_same(&offenderKey, &meta->offendingPost.poster));
  sol_log("Offsets of accountType, numPosts:");
  sol_log_64(OFFSETOF(AccountMetadata, accountType), OFFSETOF(AccountMetadata, numPosts), sizeof(AccountMetadata), 4, 5);
}