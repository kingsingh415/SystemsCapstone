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

/*
uint16_t countPosts(uint8_t* data, uint64_t length) {
  uint16_t count = 0;
  bool inPost = false;
  uint64_t currentPostCount = 0;
  sol_log("(Post type), (Post length), 0, 0, 0");
  for(uint64_t i = 0; i < length; i++) {
    if(inPost) {
      if(data[i] == POST_TERMINATOR) {
        
        sol_log_64(0);
      }
    } else {
      if(data[i] == POST_TERMINATOR) {
        break;
      }
    }
  }
  return 0;
}
*/

// TODO who should enforce null-termination of post string? 
// Currently it is assumed that the instruction data is a valid c-string
// This is probably not a good long term solution, because a malformed
// instruction could leave account data in an invalid state
uint64_t helloworld(SolParameters *params) {
  //sol_log_params(params);
  if (params->ka_num < 1) {
    sol_log("Greeted account not included in the instruction");
    return ERROR_NOT_ENOUGH_ACCOUNT_KEYS;
  }

  // Get the account to say hello to
  SolAccountInfo *greeted_account = &params->ka[0];

  // The account must be owned by the program in order to modify its data
  if (!SolPubkey_same(greeted_account->owner, params->program_id)) {
    sol_log("Greeted account does not have the correct program id");
    return ERROR_INCORRECT_PROGRAM_ID;
  }

  sol_log("Recieved post:");
  sol_log_array(params->data, params->data_len);
  sol_log((const char*)params->data);

  uint64_t newOffset = newDataOffset(greeted_account->data, greeted_account->data_len);

  sol_log("Bytes used:");
  sol_log_64(0, 0, 0, 0, newOffset);

  // The data must be large enough to hold the post
  if(greeted_account->data_len - newOffset < params->data_len) {
    sol_log("Account too small to hold new post");
    return ERROR_INVALID_ACCOUNT_DATA;
  }

  sol_memcpy(&greeted_account->data[newOffset], params->data, params->data_len);
  (*((uint16_t*)greeted_account->data)) += 1; // Increment post count

  newOffset = newDataOffset(greeted_account->data, greeted_account->data_len);
  sol_log("Bytes used:");
  sol_log_64(0, 0, 0, 0, newOffset);
  // The data must be large enough to hold an uint32_t value
  /*
  if (greeted_account->data_len < sizeof(uint32_t)) {
    sol_log("Greeted account data length too small to hold uint32_t value");
    return ERROR_INVALID_ACCOUNT_DATA;
  }
  */

  // Increment and store the number of times the account has been greeted
  //uint32_t *num_greets = (uint32_t *)greeted_account->data;
  //*num_greets += 1;

  //sol_log("Hello!");

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
