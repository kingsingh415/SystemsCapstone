/**
 * @brief C-based Helloworld BPF program
 */
#include <solana_sdk.h>

// Post type selectors
#define POST_SELECTOR 'P'
#define REPLY_SELECTOR 'R'
#define LIKE_SELECTOR 'L'
#define REPORT_SELECTOR 'X'
// Post terminator byte - posts are terminated with a single terminator, two terminators in a row mark the end of used
// data in the account
#define POST_TERMINATOR '\0'
// Defining a separate terminator for the end of the used space in the file would make usedAccountSize perform less operations?

// Returns the offset of the first byte not used for post data
// (Assumes we've already checked to ensure account has at least 2 bytes of data)
uint64_t newDataOffset(uint8_t* data, uint64_t length) {
  // If empty account
  if(*((uint16_t*)data) == 0) {
    return 2;
  }
  // Else find first 2 terminators in a row
  for(uint64_t i = 1; i < length; i++) {
    if(data[i] == POST_TERMINATOR && data[i - 1] == POST_TERMINATOR) {
      return i;
    }
  }
  return length;
}

// A unique identifier for a single post
typedef struct _PostID {
  SolPubkey poster;
  uint16_t index;
} PostID;

// Storage for a single post of any type
// Fields may be unused depending on selector
typedef struct _Post {
  uint8_t typeSelector;
  PostID id;
  const uint8_t* postBody; // WARNING: NOT NULL-TERMINATED
  uint64_t bodyLength;
} Post;

// Parse instruction data into a post struct
// Returns the number of bytes needed to store the post, or 0 if the post is invalid
uint64_t parsePost(const uint8_t* d, uint64_t len, Post* p) {
  if(len < 2) {
    return 0; // Minimum size of a post is 2 bytes (selector + 1 character post)
  }
  p->typeSelector = *d;
  switch(*d) {
  case POST_SELECTOR:
    p->postBody = &d[1]; // Body is just the rest of the post data
    p->bodyLength = len - 1;
    return 1 + p->bodyLength + 1; // Selector + body + terminator
  case REPLY_SELECTOR:
    if(len < 1 + sizeof(PostID) + 1) {
      return 0; // Minimum size of a reply is 36 bytes (selector + (32 bytes pubkey + 2 bytes index) + 1 character post)
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->postBody = &d[1 + sizeof(PostID)];
    p->bodyLength = len - 1 - sizeof(PostID);
    return 1 + sizeof(PostID) + p->bodyLength + 1;
  case LIKE_SELECTOR:
    if(len < 1 + sizeof(PostID)) {
      return false; // Minimum size of a like is 35 bytes (selector + (32 bytes pubkey + 2 bytes index))
    }
    p->id = *((PostID*)&d[1]);
    return 1 + sizeof(PostID) + 1;
  case REPORT_SELECTOR:
    if(len < 1 + sizeof(PostID)) {
      return false; // Minimum size of a report is 35 bytes (selector + (32 bytes pubkey + 2 bytes index) + 0 character report reason (optional))
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->postBody = &d[1 + sizeof(PostID)];
    p->bodyLength = len - 1 - sizeof(PostID); // Possibly 0
    return 1 + sizeof(PostID) + p->bodyLength + 1;
  default:
    return 0;
  }
  return 0;
}

// Copy the post represented by a post struct into account memory
void copyPost(Post* p, uint8_t* account) {
  // Every type of post will copy a selector byte
  *account = p->typeSelector;
  account++;
  switch(p->typeSelector) {
  case POST_SELECTOR:
    sol_memcpy(account, p->postBody, p->bodyLength);
    account[p->bodyLength] = POST_TERMINATOR;
    break;
  case REPLY_SELECTOR:
  case REPORT_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    account += sizeof(PostID);
    sol_memcpy(account, p->postBody, p->bodyLength);
    account[p->bodyLength] = POST_TERMINATOR;
    break;
  case LIKE_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    account[sizeof(PostID)] = POST_TERMINATOR;
    break;
  default:
    break;
  }
}

// Scrub the body of a post to remove any illegal characters (ie post terminator)
// void scrubPost()
// TODO

uint64_t helloworld(SolParameters *params) {
  sol_log_params(params);
  if (params->ka_num < 1) {
    sol_log("Greeted account not included in the instruction");
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // Get the account to say hello to
  SolAccountInfo *greeted_account = &params->ka[0];

  // The poster's account must have signed off on this transaction
  if(!greeted_account->is_signer) {
    sol_log("The poster's account must sign off on any transaction");
    return ERROR_MISSING_REQUIRED_SIGNATURES;
  }

  // The account must be owned by the program in order to modify its data
  if (!SolPubkey_same(greeted_account->owner, params->program_id)) {
    sol_log("Greeted account does not have the correct program id");
    return ERROR_INCORRECT_PROGRAM_ID;
  }

  //sol_log("Recieved post:");
  //sol_log_array(params->data, params->data_len);

  uint64_t newOffset = newDataOffset(greeted_account->data, greeted_account->data_len);

  //sol_log("Bytes used:");
  //sol_log_64(0, 0, 0, 0, newOffset);

  Post postData;
  uint64_t bytesNeeded = parsePost(params->data, params->data_len, &postData);
  if(bytesNeeded == 0) {
    sol_log("Invalid instruction");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  // The data must be large enough to hold the post
  if(greeted_account->data_len - newOffset < bytesNeeded) {
    sol_log("Account too small to hold new post");
    return ERROR_ACCOUNT_DATA_TOO_SMALL;
  }

  // Finally, copy the actual post into memory
  copyPost(&postData, &greeted_account->data[newOffset]);
  // Increment post count
  (*((uint16_t*)greeted_account->data)) += 1; 

  return SUCCESS;
}

extern uint64_t entrypoint(const uint8_t *input) {
  sol_log("Helloworld C program entrypoint");

  SolAccountInfo accounts[1];
  SolParameters params = (SolParameters){.ka = accounts};

  if (!sol_deserialize(input, &params, SOL_ARRAY_SIZE(accounts))) {
    return ERROR_INVALID_ARGUMENT;
  }

  return helloworld(&params);
}