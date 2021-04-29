/**
 * @brief C-based Solana forum program
 */
#include <solana_sdk.h>

// Structures and constants
// ----------------------------------------------------------------------------
/*
Possible types of accounts
Starts at 1 so that an account with 0 in the first byte is always 
uninitialized
*/
typedef enum {
  User = 1,
  Petition = 2
} AccountType;

// A unique identifier for a single post
typedef struct {
  SolPubkey poster;
  uint16_t index;
} PostID;

// User account metadata
typedef struct {
  uint8_t accountType;
  uint16_t numPosts;
  char username[32]; // null-terminated if shorter than 32 bytes
  uint64_t reputation;
} AccountMetadata;

// A single petition signature
typedef struct {
  SolPubkey signer;
  uint8_t vote;
} PetitionSignature;

// Petition account data
typedef struct {
  uint8_t accountType;
  PostID offendingPost;
  uint8_t completed;
  int64_t netTally;
  uint32_t reputationRequirement;
  uint16_t numSignatures;
} PetitionAccountMeta;

/*
Post format:

width       name          type          description
-----------------------------------------------------------------------------
2           length        uint16_t      size of the rest of the post
1           typeSelector  uint8_t       type selector (ASCII P, R, L, or X)

The rest is dependent on the value of typeSelector:
-----If typeSelector == 'P'--------------------------------------------------
length-1    postBody      uint8_t[]     utf-8 body of the post
-----If 'R' or 'X'-----------------------------------------------------------
34          id            PostID        the post referenced by this post
length-35   postBody      uint8_t[]     utf-8 body of the post
-----If 'L'------------------------------------------------------------------
34          id            PostID        the post being liked by this post
*/

typedef union {
  uint8_t* mutable;
  const uint8_t* immutable;
} String;

// Storage for a single post of any type
typedef struct {
  uint16_t length;
  uint8_t typeSelector;
  PostID id;
  // Violate const safety with union
  String body;
  uint64_t bodyLength;
} Post;

// Storage for a reply
typedef struct {
  uint16_t length;
  uint8_t typeSelector;
  PostID id;
  uint8_t postBody;
} Reply;

// Constants
// Minimum size of a single post of any type
// (2 bytes size, 1 byte type, 1 byte body)
#define MIN_POST_SIZE 4
// The maximum size of a post (including metadata) is 65535 bytes
#define MAX_INSTRUCTION_LENGTH 0xFFFF
// The size of a new petition account instruction
// selector + post index
#define CREATE_PETITION_INSTRUCTION_SIZE (1 + sizeof(uint16_t))
// The maximum number of slots in a petition
// (585 as of 4/29/21)
#define MAX_PETITION_SIZE (HEAP_LENGTH / sizeof(SolAccountInfo))

// Instruction type selectors
// Basic forum instructions
#define POST_SELECTOR 'P'
#define REPLY_SELECTOR 'R'
#define LIKE_SELECTOR 'L'
#define REPORT_SELECTOR 'X'

// Petition instructions
#define VOTE_SELECTOR 'V'
#define CREATE_PETITION_SELECTOR 'C'
#define PROCESS_PETITION_SELECTOR 'F'

// Misc.
#define SET_USERNAME_SELECTOR 's'
#define REDACTION_BYTE 'x'

// END structures and constants
// ----------------------------------------------------------------------------

// Helper functions 
// ---------------------------------------------------------------------------- 
// Returns the offset of the first byte not used for post data
uint64_t newPostOffset(uint8_t* data, uint64_t length) {
  // If empty account
  AccountMetadata* meta = (AccountMetadata*)data;
  if(meta->numPosts == 0) {
    return sizeof(AccountMetadata);
  }
  // Else find first space not used by posts
  uint64_t offset = sizeof(AccountMetadata);
  for(;;) {
    uint16_t advance = *((uint16_t*)&data[offset]);
    // If post length is 0, we've found uninitialized data
    if(advance == 0) {
      return offset;
    }
    offset += advance + sizeof(uint16_t);
    // Account is full!
    if(offset >= length) {
      return length;
    }
  }
  return offset;
}

/*
Parse instruction data into a post struct
Returns the number of bytes needed to store the post, or 0 if the 
post is invalid
*/
uint64_t parsePost(const uint8_t* d, uint64_t len, Post* p) {
  if(len < 4) {
    return 0; // Minimum size of a post is 4 bytes:
              // (size + selector + 1 character post)
  }
  p->typeSelector = *d;
  p->length = len;
  switch(*d) {
  case POST_SELECTOR:
    p->body.immutable = &d[1]; // Body is just the rest of the post data
    p->bodyLength = len - 1;
    return 1 + p->bodyLength + sizeof(uint16_t); // Selector + body + size
  case REPLY_SELECTOR:
    if(len < 1 + sizeof(PostID) + 1) {
      return 0; // Minimum size of a reply is 36 bytes:
                // (selector + (32 bytes pubkey + 2 bytes index) 
                // + 1 character post)
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->body.immutable = &d[1 + sizeof(PostID)];
    p->bodyLength = len - 1 - sizeof(PostID);
    return sizeof(uint16_t) + 1 + sizeof(PostID) + p->bodyLength;
  case LIKE_SELECTOR:
    if(len != 1 + sizeof(PostID)) {
      return 0; // Size of a like is 35 bytes:
                // (selector + (32 bytes pubkey + 2 bytes index))
    }
    p->id = *((PostID*)&d[1]);
    return sizeof(uint16_t) + 1 + sizeof(PostID);
  case REPORT_SELECTOR:
    if(len < 1 + sizeof(PostID) + 1) {
      return 0; // Minimum size of a report is 36 bytes:
                // (selector + (32 bytes pubkey + 2 bytes index) 
                // + 1 character report reason)
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->body.immutable = &d[1 + sizeof(PostID)];
    p->bodyLength = len - 1 - sizeof(PostID);
    return sizeof(uint16_t) + 1 + sizeof(PostID) + p->bodyLength;
  default:
    return 0;
  }
  return 0;
}

// Copy the post represented by a post struct into account memory
void copyPost(Post* p, uint8_t* account) {
  // Every type of post will copy a selector byte and size
  sol_memcpy(account, &p->length, sizeof(uint16_t));
  account[2] = p->typeSelector;
  account += 3;
  switch(p->typeSelector) {
  case POST_SELECTOR:
    sol_memcpy(account, p->body.immutable, p->bodyLength);
    break;
  case REPLY_SELECTOR:
  case REPORT_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    account += sizeof(PostID);
    sol_memcpy(account, p->body.immutable, p->bodyLength);
    break;
  case LIKE_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    break;
  default:
    break;
  }
}

bool isInitialized(uint8_t* data) {
  return *data != 0;
}

void initializeUserAccount(uint8_t* data, uint64_t length) {
  AccountMetadata* meta = (AccountMetadata*)data;
  meta->accountType = User;
}

// Number of signatures that will fit in the account of given length
uint64_t signatureCapacity(uint64_t length) {
  return (length - sizeof(PetitionAccountMeta)) / sizeof(PetitionSignature);
}

// Returns the minimum reputation needed to vote on a petition against
// a user with the given rep
uint64_t votingRequirement(uint64_t offenderReputation, uint64_t numVotes) {
  return (offenderReputation / numVotes) + 1;
}

void initializePetitionAccount(uint8_t* data, uint64_t length, PostID* offender,
                               uint8_t* offenderData, uint64_t offenderDataLength) {
  PetitionAccountMeta* account = (PetitionAccountMeta*)data;
  account->accountType = Petition;
  account->offendingPost = *offender;
  account->numSignatures = 0;
  // set reputation requirement so that a majority vote will always win
  AccountMetadata* offenderMeta = (AccountMetadata*)offenderData;
  account->reputationRequirement = votingRequirement(offenderMeta->reputation, signatureCapacity(length));
  account->completed = 0;
}

// Returns true if the given user can vote on the given petition
bool meetsVotingRequirements(SolAccountInfo* user, SolAccountInfo* petition) {
  AccountMetadata* userMeta = (AccountMetadata*)user->data;
  PetitionAccountMeta* petitionMeta = (PetitionAccountMeta*)petition->data;

  return userMeta->reputation >= petitionMeta->reputationRequirement;
}

// Returns true if the given user has already voted on the given petition
bool hasVoted(SolAccountInfo* user, SolAccountInfo* petition) {
  PetitionAccountMeta* petitionMeta = (PetitionAccountMeta*)petition->data;
  PetitionSignature* signatures = (PetitionSignature*)(&petition->data[sizeof(PetitionAccountMeta)]);
  for(uint64_t i = 0; i < petitionMeta->numSignatures; i++) {
    if(SolPubkey_same(&signatures[i].signer, user->key)) {
      return true;
    }
  }
  return false;
}

// Gets the byte offset of post with given index
uint64_t postOffset(uint8_t* data, uint16_t index) {
  AccountMetadata* meta = (AccountMetadata*)data;
  uint64_t offset = sizeof(AccountMetadata);
  for(uint16_t i = 0; i < index; i++) {
    uint16_t advance = *((uint16_t*)&data[offset]);
    offset += advance + sizeof(uint16_t);
  }
  return offset;
}

// Replaces the body of a post with ASCII 'x'
// Will break if the post given doesn't have a body
void redactPost(SolAccountInfo* offender, uint16_t index) {
  uint64_t redactedPostOffset = postOffset(offender->data, index);
  uint16_t redactedPostLength = *(uint16_t*)(&offender->data[redactedPostOffset]);
  Post redactedPost;
  if(parsePost(&offender->data[redactedPostOffset + sizeof(uint16_t)], redactedPostLength, &redactedPost) == 0)
  {
    sol_log("Failed to parse post from account data, skipping redaction");
    return;
  }
  // Redact the post
  for(uint16_t i = 0; i < redactedPost.bodyLength; i++) {
    redactedPost.body.mutable[i] = 'x';
  }
}

// Processes the outcome of a vote
// A tie is broken by the petition failing
// The first account must be the petition account
// The second account must be the offender's account
// The rest of the accounts must be the accounts in the petition in the order they appear
uint64_t processPetitionOutcome(SolParameters* params) {
  if(params->ka_num < 3) {
    sol_log("Must provide at least 3 accounts to process a petition, got:");
    sol_log_64(params->ka_num, 0, 0, 0, 0);
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // No instruction data is required
  if(params->data_len != 0) {
    sol_log("No instruction data is necessary for this instruction");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  SolAccountInfo* petitionAccount = &params->ka[0];
  SolAccountInfo* offenderAccount = &params->ka[1];
  SolAccountInfo* voterAccounts = &params->ka[2];

  if(!isInitialized(petitionAccount->data)) {
    sol_log("This petition is not initialized");
    return ERROR_UNINITIALIZED_ACCOUNT;
  }

  // Check if the petition is already completed
  PetitionAccountMeta* petition = (PetitionAccountMeta*)(petitionAccount->data);
  if(petition->completed) {
    sol_log("Petition is already completed.");
    return ERROR_INVALID_ACCOUNT_DATA;
  }
  
  if(petition->numSignatures != signatureCapacity(petitionAccount->data_len)) {
    sol_log("Petition is not full yet.");
    sol_log_64(petition->numSignatures, signatureCapacity(petitionAccount->data_len), 0, 0, 0);
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  int64_t voteTally = 0;
  PetitionAccountMeta* petitionMeta = (PetitionAccountMeta*)petitionAccount->data;
  PetitionSignature* signatureArray = (PetitionSignature*)&petitionAccount->data[sizeof(PetitionAccountMeta)];
  // Before modifying anything, reject the transaction if any of the account parameters are incorrect
  if(!SolPubkey_same(&petitionMeta->offendingPost.poster, offenderAccount->key)) {
    sol_log("Second account parameter must be the offender's account");
    return ERROR_INVALID_ARGUMENT;
  }
  if(params->ka_num - 2 != petitionMeta->numSignatures) {
    sol_log("Invalid number of account parameters");
    sol_log("Expected:");
    sol_log_64(petitionMeta->numSignatures, 0, 0, 0, 0);
    sol_log("Got:");
    sol_log_64(params->ka_num - 2, 0, 0, 0, 0);
    return ERROR_INVALID_ARGUMENT;
  }
  for(uint64_t i = 0; i < petitionMeta->numSignatures; i++) {
    // Check to ensure that the correct accounts were passed in
    // in the correct order
    if(!SolPubkey_same(&signatureArray[i].signer, voterAccounts[i].key)) {
      sol_log("Invalid account parameter for petition slot:");
      sol_log_64(i, 0, 0, 0, 0);
      sol_log("Expected:");
      sol_log_pubkey(&signatureArray[i].signer);
      sol_log("Got:");
      sol_log_pubkey(voterAccounts[i].key);
      return ERROR_INVALID_ARGUMENT;
    }
  }

  // We may complete the petition.
  petitionMeta->completed = true;

  for(uint64_t i = 0; i < petitionMeta->numSignatures; i++) {
    if(signatureArray[i].vote) {
      voteTally++;
      //sol_log("Counted 1 vote for:");
    }
    else {
      //sol_log("Counted 1 vote against:");
      voteTally--;
    }
    //sol_log_64(signatureArray[i].vote, 0, 0, 0, 0);
  }
  
  bool petitionOutcome = voteTally > 0;
  // The petition succeeds! Redact the post.
  if(petitionOutcome) {
    sol_log("Petition succeeded!");
    //sol_assert(SolPubkey_same(offenderAccount->key, &petitionMeta->offendingPost.poster));
    redactPost(offenderAccount, petitionMeta->offendingPost.index);
    AccountMetadata* offenderMeta = (AccountMetadata*)offenderAccount->data;
    offenderMeta->reputation -= voteTally * petitionMeta->reputationRequirement;
  }
  // The petition failed.
  else {
    sol_log("Petition failed.");
  }
  // Distribute rewards and penalties
  for(uint64_t i = 0; i < petitionMeta->numSignatures; i++) {
    AccountMetadata* voterMeta = (AccountMetadata*)&voterAccounts[i].data;
    if(signatureArray[i].vote == petitionOutcome) {
      // Reward this user
      sol_log("Rewarding user:");
      voterMeta->reputation += petitionMeta->reputationRequirement;
    }
    else {
      // Penalize this user
      sol_log("Penalizing user:");
      voterMeta->reputation -= petitionMeta->reputationRequirement;
    }
    sol_log_pubkey(&signatureArray[i].signer);
    sol_log("For this amount of reputation:");
    sol_log_64(petitionMeta->reputationRequirement, 0, 0, 0, 0);
  }
  sol_log("Vote tally:");
  sol_log_64(voteTally, 0, 0, 0, 0);

  return SUCCESS;
}

// Ensure a user account is initialized
uint64_t ensureInitializedUser(SolAccountInfo* account) {
  /*
  If the poster's account doesn't even have enough to store metadata, 
  then it is invalid
  */
  if(account->data_len < sizeof(AccountMetadata)) {
    sol_log("The poster's account is too small to be valid");
    return ERROR_ACCOUNT_DATA_TOO_SMALL;
  }

  /*
  Check if the account is already initialized
  */
  if(!isInitialized(account->data)) {
    initializeUserAccount(account->data, account->data_len);
  }

  return SUCCESS;
}

// END helper functions 
// ---------------------------------------------------------------------------- 

// Processing functions for each type of instruction

/*
Post processor
Note that a 'post' also includes likes, reports, and replies
*/
uint64_t processPost(SolParameters* params) {
  SolAccountInfo* posterAccount = &params->ka[0];

  // Reject any posts that are too long for a uint16_t
  if(params->data_len > MAX_INSTRUCTION_LENGTH) {
    sol_log("The post is too long");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  if(!posterAccount->is_signer) {
    sol_log("The poster must sign this instruction");
    return ERROR_MISSING_REQUIRED_SIGNATURES;
  }

  // Ensure that the account is initialized
  uint64_t result = ensureInitializedUser(posterAccount);
  if(result != SUCCESS) {
    return result;
  }

  // Process the post instruction

  //sol_log("Recieved post:");
  //sol_log_array(params->data, params->data_len);

  // Find the offset at which a new post would be stored
  uint64_t newOffset = newPostOffset(posterAccount->data, posterAccount->data_len);

  //sol_log("Bytes used:");
  //sol_log_64(0, 0, 0, 0, newOffset);
  //sol_log("Data to be added:");

  Post postData;
  uint64_t bytesNeeded = parsePost(params->data, params->data_len, &postData);
  //sol_log_64(0, 0, 0, 0, bytesNeeded);
  if(bytesNeeded == 0) {
    sol_log("Invalid instruction");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  // The data must be large enough to hold the post
  if(newOffset + bytesNeeded > posterAccount->data_len) {
    //sol_log_64(newOffset, bytesNeeded, posterAccount->data_len, 0, 0);
    sol_log("Account too small to hold new post");
    return ERROR_ACCOUNT_DATA_TOO_SMALL;
  }

  // Finally, copy the actual post into memory
  copyPost(&postData, &posterAccount->data[newOffset]);
  // Increment post count
  AccountMetadata* meta = (AccountMetadata*)(posterAccount->data);
  meta->numPosts += 1;

  return SUCCESS;
}

/*
Vote insruction processor
Expects 2 accounts:
  -The account voting
  -The account containing the petition
Only the first must be a signer.
*/
uint64_t processVote(SolParameters* params) {
  if(params->ka_num != 2) {
    sol_log("2 account parameters are needed to vote, Got:");
    sol_log_64(params->ka_num, 0, 0, 0, 0);
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // Instruction data is a single byte indicating the boolean vote
  if(params->data_len != 2) {
    sol_log("Vote instructions must be 2 bytes, Got:");
    sol_log_64(params->data_len, 0, 0, 0, 0);
    return ERROR_INVALID_INSTRUCTION_DATA;
  }
  bool userVote = params->data[1] != 0;

  SolAccountInfo* votingAccount = &params->ka[0];
  SolAccountInfo* petitionAccount = &params->ka[1];

  if(!votingAccount->is_signer) {
    sol_log("The voter must sign this instruction");
    return ERROR_MISSING_REQUIRED_SIGNATURES;
  }

  if(!isInitialized(petitionAccount->data)) {
    sol_log("Cannot vote on an uninitialized petition");
    return ERROR_UNINITIALIZED_ACCOUNT;
  }

  // Check if the petition is already completed
  PetitionAccountMeta* petition = (PetitionAccountMeta*)(petitionAccount->data);
  if(petition->numSignatures >= signatureCapacity(petitionAccount->data_len)) {
    sol_log("Petition is already full");
    sol_log_64(petition->numSignatures, signatureCapacity(petitionAccount->data_len), 0, 0, 0);
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  // Check if the user has already voted on this petition
  if(hasVoted(votingAccount, petitionAccount)) {
    sol_log("This user has already voted on this petition");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  // Check if the user's account meets the voting requirements
  if(!meetsVotingRequirements(votingAccount, petitionAccount)) {
    sol_log("The user does not have enough reputation to vote on this petition");
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  // We can vote!
  PetitionSignature* signatureArray = (PetitionSignature*)&petitionAccount->data[sizeof(PetitionAccountMeta)];
  PetitionSignature userSignature = { .signer = *votingAccount->key, .vote = userVote };
  sol_memcpy(&signatureArray[petition->numSignatures], &userSignature, sizeof(PetitionSignature));
  petition->numSignatures++;

  // If that was the last signature, determine the outcome of the vote
  // This is now handled in a separate transaction
  /*
  if(petition->numSignatures == signatureCapacity(petitionAccount->data_len)) {
    processPetitionOutcome(petitionAccount, offendingAccount);
  }
  */

  return SUCCESS;
}

/*
Process an instruction to initialize a new petition account
Expects 2 accounts:
  -The account that will contain the petition (uninitialized)
  -The account that the petition is against (offending account)
*/
uint64_t createPetition(SolParameters* params) {
  
  if(params->ka_num != 2) {
    sol_log("2 account parameters are needed to create a new petition, Got:");
    sol_log_64(params->ka_num, 0, 0, 0, 0);
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // Valid instruction data is always the same length
  if(params->data_len != CREATE_PETITION_INSTRUCTION_SIZE) {
    sol_log("Create petition instructions must be 3 bytes, Got:");
    sol_log_64(params->data_len, 0, 0, 0, 0);
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  SolAccountInfo* petitionAccount = (SolAccountInfo*)&params->ka[0];
  SolAccountInfo* offendingAccount = (SolAccountInfo*)&params->ka[1];

  if(!petitionAccount->is_signer) {
    sol_log("The petition account must sign");
    return ERROR_MISSING_REQUIRED_SIGNATURES;
  }

  if(isInitialized(petitionAccount->data)) {
    sol_log("Cannot create a petition on an initialized account");
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  if(signatureCapacity(petitionAccount->data_len) > MAX_PETITION_SIZE) {
    sol_log("Cannot create a petition with more than:");
    sol_log_64(MAX_PETITION_SIZE, 0, 0, 0, 0);
    sol_log("Signature slots");
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  PostID offendingPost;
  offendingPost.poster = *offendingAccount->key;
  offendingPost.index = *(uint16_t*)(&params->data[1]);
  initializePetitionAccount(petitionAccount->data, petitionAccount->data_len, &offendingPost, offendingAccount->data, offendingAccount->data_len);

  return SUCCESS;
}
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
// Main function and entry point
uint64_t helloworld(SolParameters *params) {
  if (params->ka_num < 1) {
    sol_log("No accounts were included in the instruction");
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // The first account is always the user requesting the transaction
  SolAccountInfo* userAccount = &params->ka[0];

  // The account must be owned by the program in order to modify its data
  if (!SolPubkey_same(userAccount->owner, params->program_id)) {
    sol_log("user's account does not have the correct program id");
    return ERROR_INCORRECT_PROGRAM_ID;
  }

  // Process the instruction
  switch(*params->data) {
  case POST_SELECTOR:
  case REPLY_SELECTOR:
  case LIKE_SELECTOR:
  case REPORT_SELECTOR:
    return processPost(params);
  case VOTE_SELECTOR:
    return processVote(params);
  case CREATE_PETITION_SELECTOR:
    return createPetition(params);
  case PROCESS_PETITION_SELECTOR:
    return processPetitionOutcome(params);
  default:
    sol_log("Invalid instruction selector");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }
}

extern uint64_t entrypoint(const uint8_t *input) {
  sol_log("Solana Forum C program entrypoint");

  SolParameters params = (SolParameters){.ka = (SolAccountInfo*)HEAP_START_ADDRESS};

  if (!sol_deserialize(input, &params, HEAP_LENGTH / sizeof(SolAccountInfo))) {
    return ERROR_INVALID_ARGUMENT;
  }

  // Check to make sure that the number of account parameters hasn't exceeded the heap size
  if(params.ka_num > (HEAP_LENGTH / sizeof(SolAccountInfo))) {
    return ERROR_INVALID_ARGUMENT;
  }

  return helloworld(&params);
}
