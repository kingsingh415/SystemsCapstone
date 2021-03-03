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
  console.log("Let's say hello to a Solana account...");
  const readlineSync = require('readline-sync');
  let postBody = readlineSync.question('Enter your post: ', { hideEchoBack: false });
  
  // Establish connection to the cluster
  await establishConnection();

  // Determine who pays for the fees
  await establishPayer();

  // Load the program if not already loaded
  await loadProgram();

  // Say hello to an account
  await sayHello(postBody);

  // Find out how many times that account has been greeted
  await reportHellos();

  // Get accounts owned by the program
  await reportAccounts();

  console.log('Success');
}

main().then(
  () => process.exit(),
  err => {
    console.error(err);
    process.exit(-1);
  },
);
