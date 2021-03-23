/**
 * @brief C-based Solana forum program
 */
#include <solana_sdk.h>

// Structures
// Account metadata
typedef struct {
  uint16_t numPosts;
} AccountMetadata;

// A unique identifier for a single post
typedef struct {
  SolPubkey poster;
  uint16_t index;
} PostID;

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

// Storage for a single post of any type
typedef struct {
  uint16_t length;
  uint8_t typeSelector;
  PostID id;
  const uint8_t* postBody; // NOT NULL-TERMINATED
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

// Post type selectors
#define POST_SELECTOR 'P'
#define REPLY_SELECTOR 'R'
#define LIKE_SELECTOR 'L'
#define REPORT_SELECTOR 'X'

// Returns the offset of the first byte not used for post data
uint64_t newDataOffset(uint8_t* data, uint64_t length) {
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

// Parse instruction data into a post struct
// Returns the number of bytes needed to store the post, or 0 if the post is invalid
uint64_t parsePost(const uint8_t* d, uint64_t len, Post* p) {
  if(len < 4) {
    return 0; // Minimum size of a post is 4 bytes (size + selector + 1 character post)
  }
  p->typeSelector = *d;
  p->length = len;
  switch(*d) {
  case POST_SELECTOR:
    p->postBody = &d[1]; // Body is just the rest of the post data
    p->bodyLength = len - 1;
    return 1 + p->bodyLength + sizeof(uint16_t); // Selector + body + size
  case REPLY_SELECTOR:
    if(len < 1 + sizeof(PostID) + 1) {
      return 0; // Minimum size of a reply is 36 bytes (selector + (32 bytes pubkey + 2 bytes index) + 1 character post)
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->postBody = &d[1 + sizeof(PostID)];
    p->bodyLength = len - 1 - sizeof(PostID);
    return sizeof(uint16_t) + 1 + sizeof(PostID) + p->bodyLength;
  case LIKE_SELECTOR:
    if(len != 1 + sizeof(PostID)) {
      return 0; // Size of a like is 35 bytes (selector + (32 bytes pubkey + 2 bytes index))
    }
    p->id = *((PostID*)&d[1]);
    return sizeof(uint16_t) + 1 + sizeof(PostID);
  case REPORT_SELECTOR:
    if(len < 1 + sizeof(PostID) + 1) {
      return 0; // Minimum size of a report is 36 bytes (selector + (32 bytes pubkey + 2 bytes index) + 1 character report reason)
    }
    p->id = *((PostID*)&d[1]); // Assume data is already little-endian
    p->postBody = &d[1 + sizeof(PostID)];
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
    sol_memcpy(account, p->postBody, p->bodyLength);
    break;
  case REPLY_SELECTOR:
  case REPORT_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    account += sizeof(PostID);
    sol_memcpy(account, p->postBody, p->bodyLength);
    break;
  case LIKE_SELECTOR:
    sol_memcpy(account, &p->id, sizeof(PostID));
    break;
  default:
    break;
  }
}

// Scrub the body of a post to remove any illegal characters (ie post terminator)
// void scrubPost()
// TODO

uint64_t helloworld(SolParameters *params) {
  //sol_log("Instruction data:");
  //sol_log_array(params->data, params->data_len);
  if (params->ka_num < 1) {
    sol_log("Poster's account not included in the instruction");
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // The poster's account is the first account in the instruction
  SolAccountInfo *greeted_account = &params->ka[0];

  // If the poster's account doesn't even have enough to store metadata, then it is invalid
  if(greeted_account->data_len < sizeof(AccountMetadata)) {
    sol_log("The poster's account is too small to be valid");
    return ERROR_ACCOUNT_DATA_TOO_SMALL;
  }

  // The poster's account must have signed off on this transaction
  if(!greeted_account->is_signer) {
    sol_log("The poster's account must sign off on any transaction");
    return ERROR_MISSING_REQUIRED_SIGNATURES;
  }

  // The account must be owned by the program in order to modify its data
  if (!SolPubkey_same(greeted_account->owner, params->program_id)) {
    sol_log("Poster's account does not have the correct program id");
    return ERROR_INCORRECT_PROGRAM_ID;
  }

  // Reject any posts that are too long for a uint16_t
  if(params->data_len > MAX_INSTRUCTION_LENGTH) {
    sol_log("The post is too long");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  //sol_log("Recieved post:");
  //sol_log_array(params->data, params->data_len);

  // Find the offset at which a new post would be stored
  uint64_t newOffset = newDataOffset(greeted_account->data, greeted_account->data_len);

  sol_log("Bytes used:");
  sol_log_64(0, 0, 0, 0, newOffset);
  sol_log("Data to be added:");

  Post postData;
  uint64_t bytesNeeded = parsePost(params->data, params->data_len, &postData);
  sol_log_64(0, 0, 0, 0, bytesNeeded);
  if(bytesNeeded == 0) {
    sol_log("Invalid instruction");
    return ERROR_INVALID_INSTRUCTION_DATA;
  }

  // The data must be large enough to hold the post
  if(newOffset + bytesNeeded > greeted_account->data_len) {
    sol_log("Account too small to hold new post");
    return ERROR_ACCOUNT_DATA_TOO_SMALL;
  }

  // Finally, copy the actual post into memory
  copyPost(&postData, &greeted_account->data[newOffset]);
  // Increment post count
  AccountMetadata* meta = (AccountMetadata*)(greeted_account->data);
  meta->numPosts += 1;

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
