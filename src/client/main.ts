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
  getArrayOfPosts
} from './hello_world';



async function main() {
  console.log("--------------------Solana forum demo--------------------");
  const readlineSync = require('readline-sync');
  let options = ["View Posts", "New Post", "Like Post"];
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
      // await reportAccounts();
      await reportAccounts();
      /*
      let view = await getArrayOfPosts();
      for (var val of view) {
        console.log(val); 
      }
      */
      break;
    case 1:
      
      // Enter post text
      let postBody = readlineSync.question('Enter your post: ', { hideEchoBack: false });

      // Say hello to an account
      await sayHello(postBody, "post");

      // Find out how many times that account has been greeted
      await reportHellos();

      // Get accounts owned by the program
      // await reportAccounts();

      /*let postRet = await getArrayOfPosts();
      for (var val of postRet) {
        console.log(val);
      }*/

      break;
    /*
    case 2:
      let ret = await getArrayOfPosts();
      for (var val of ret) {
        console.log(val);
      }
      let choice = readlineSync.question('Enter which post number you would like to send a like to: ', { hideEchoBack: false });
      var num: number = +choice;
      var split = ret[num].split(" - ");
      // console.log(split);
      await sayHello(split[2], "like");

      // Find out how many times that account has been greeted
      await reportHellos();

      // Get accounts owned by the program
      let viewRet = await getArrayOfPosts();
      console.log(viewRet);
      break
    */
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
