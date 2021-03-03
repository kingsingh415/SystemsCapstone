/**
 * Hello world
 */

import {
  establishConnection,
  establishPayer,
  loadProgram,
  sayHello,
  reportHellos,
  reportAccounts,
} from './hello_world';



async function main() {
  console.log("--------------------Solana forum demo--------------------");
  const readlineSync = require('readline-sync');
  let options = ["View posts", "New post"];
  let response = readlineSync.keyInSelect(options, "Choose one (New post assumes you have a valid store)")
  
  // Establish connection to the cluster
  await establishConnection();

  // Determine who pays for the fees
  await establishPayer();

  // Load the program if not already loaded
  await loadProgram();

  switch(response) {
    case 0:
      // Get accounts owned by the program
      await reportAccounts();
      break;
    case 1:
      // Enter post text
      let postBody = readlineSync.question('Enter your post: ', { hideEchoBack: false });

      // Say hello to an account
      await sayHello(postBody);

      // Find out how many times that account has been greeted
      await reportHellos();

      // Get accounts owned by the program
      await reportAccounts();
      break;
    default:
      break;
  }
  console.log('Success');
}

main().then(
  () => process.exit(),
  err => {
    console.error(err);
    process.exit(-1);
  },
);
