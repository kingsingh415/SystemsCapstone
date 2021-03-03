/* eslint-disable @typescript-eslint/no-unsafe-assignment */
/* eslint-disable @typescript-eslint/no-unsafe-member-access */
/* eslint-disable @typescript-eslint/no-unsafe-call */
/* eslint-disable @typescript-eslint/ban-ts-comment */

import {
  Account,
  Connection,
  BpfLoader,
  BPF_LOADER_PROGRAM_ID,
  PublicKey,
  LAMPORTS_PER_SOL,
  SystemProgram,
  TransactionInstruction,
  Transaction,
  sendAndConfirmTransaction,
} from '@solana/web3.js';
import fs, { read } from 'mz/fs';

// @ts-ignore
import BufferLayout from 'buffer-layout';
const lo = BufferLayout;

import {url, urlTls} from './util/url';
import {Store} from './util/store';
import {newAccountWithLamports} from './util/new-account-with-lamports';

/**
 * Connection to the network
 */
let connection: Connection;

/**
 * Connection to the network
 */
let payerAccount: Account;

/**
 * Hello world's program id
 */
let programId: PublicKey;

/**
 * The public key of the account we are saying hello to
 */
let greetedPubkey: PublicKey;

const pathToProgram = 'dist/program/helloworld.so';

/**
 * Layout of a single post
 */
const postLayout = BufferLayout.struct([
  //BufferLayout.u32('numGreets'),
  BufferLayout.u8('type'),
  BufferLayout.cstr('body'),
]);

/**
 * Layout of account data
 */
const accountLayout = BufferLayout.struct([
  BufferLayout.u16('numPosts'),
  BufferLayout.seq(postLayout, BufferLayout.offset(BufferLayout.u16(), -1), 'posts'),
]);

/**
 * Layout of the greeted account data
 */
const greetedAccountDataLayout = BufferLayout.struct([
  //BufferLayout.seq(BufferLayout.u8(), 1024, 'account_data'),
  BufferLayout.seq(BufferLayout.u8(), 1024, 'account_data'),
]);

function printAccountPosts(d: Buffer) {
  const postCount = d.readUInt16LE(0);
  console.log("# of posts on account:", postCount);
  //const accountData = accountLayout.decode(d);
  //console.log(accountData.posts);
  // For now, just print with a for loop
  let readType = false;
  let currentPost = "";
  let i = 2;
  for(; i < d.length; i++) {
    // Stop after reading 2 terminators in a row
    if(d.readUInt8(i) == 0 && d.readUInt8(i - 1) == 0) {
      return;
    }
    if(!readType) {
      process.stdout.write("Type: " + String.fromCharCode(d.readUInt8(i)));
      readType = true;
    }
    else if(d.readUInt8(i) == 0) {
      console.log("\tBody:", currentPost);
      currentPost = "";
      readType = false;
    }
    else {
      currentPost += String.fromCharCode(d.readUInt8(i));
    }
  }
  console.log("Account has used", i, "out of", d.length, "available bytes");
}

/**
 * Establish a connection to the cluster
 */
export async function establishConnection(): Promise<void> {
  connection = new Connection(url, 'singleGossip');
  const version = await connection.getVersion();
  console.log('Connection to cluster established:', url, version);
}

/**
 * Establish an account to pay for everything
 */
export async function establishPayer(): Promise<void> {
  if (!payerAccount) {
    let fees = 0;
    const {feeCalculator} = await connection.getRecentBlockhash();

    // Calculate the cost to load the program
    const data = await fs.readFile(pathToProgram);
    const NUM_RETRIES = 500; // allow some number of retries
    fees +=
      feeCalculator.lamportsPerSignature *
        (BpfLoader.getMinNumSignatures(data.length) + NUM_RETRIES) +
      (await connection.getMinimumBalanceForRentExemption(data.length));

    // Calculate the cost to fund the greeter account
    fees += await connection.getMinimumBalanceForRentExemption(
      greetedAccountDataLayout.span,
    );

    // Calculate the cost of sending the transactions
    fees += feeCalculator.lamportsPerSignature * 100; // wag

    // Fund a new payer via airdrop
    payerAccount = await newAccountWithLamports(connection, fees);
  }

  const lamports = await connection.getBalance(payerAccount.publicKey);
  console.log(
    'Using account',
    payerAccount.publicKey.toBase58(),
    'containing',
    lamports / LAMPORTS_PER_SOL,
    'Sol to pay for fees',
  );
}

/**
 * Load the hello world BPF program if not already loaded
 */
export async function loadProgram(): Promise<void> {
  const store = new Store();

  // Check if the program has already been loaded
  try {
    const config = await store.load('config.json');
    programId = new PublicKey(config.programId);
    greetedPubkey = new PublicKey(config.greetedPubkey);
    await connection.getAccountInfo(programId);
    console.log('Program already loaded to account', programId.toBase58());
    return;
  } catch (err) {
    // try to load the program
  }

  // Load the program
  console.log('Loading hello world program...');
  const data = await fs.readFile(pathToProgram);
  const programAccount = new Account();
  await BpfLoader.load(
    connection,
    payerAccount,
    programAccount,
    data,
    BPF_LOADER_PROGRAM_ID,
  );
  programId = programAccount.publicKey;
  console.log('Program loaded to account', programId.toBase58());

  // Create the greeted account
  const greetedAccount = new Account();
  greetedPubkey = greetedAccount.publicKey;
  console.log('Creating account', greetedPubkey.toBase58(), 'to say hello to');
  const space = greetedAccountDataLayout.span;
  const lamports = await connection.getMinimumBalanceForRentExemption(
    greetedAccountDataLayout.span,
  );
  const transaction = new Transaction().add(
    SystemProgram.createAccount({
      fromPubkey: payerAccount.publicKey,
      newAccountPubkey: greetedPubkey,
      lamports,
      space,
      programId,
    }),
  );
  await sendAndConfirmTransaction(
    connection,
    transaction,
    [payerAccount, greetedAccount],
    {
      commitment: 'singleGossip',
      preflightCommitment: 'singleGossip',
    },
  );

  // Save this info for next time
  await store.save('config.json', {
    url: urlTls,
    programId: programId.toBase58(),
    greetedPubkey: greetedPubkey.toBase58(),
  });
}

/**
 * Say hello
 */
export async function sayHello(body: string): Promise<void> {
  console.log('Saying hello to', greetedPubkey.toBase58());

  /*
  rl.on("close", function() {
      console.log("\nBYE BYE !!!");
      process.exit(0);
  });
  */
  //const post = Buffer.from('Ptest\0');
  const post = Buffer.from('P' + body + '\0');
  console.log("Length of post:", post.length);
  const instruction = new TransactionInstruction({
    keys: [{pubkey: greetedPubkey, isSigner: false, isWritable: true}],
    programId,
    data: post,//Buffer.alloc(0), // All instructions are hellos
  });
  await sendAndConfirmTransaction(
    connection,
    new Transaction().add(instruction),
    [payerAccount],
    {
      commitment: 'singleGossip',
      preflightCommitment: 'singleGossip',
    },
  );
}

/**
 * Report the number of times the greeted account has been said hello to
 */
export async function reportHellos(): Promise<void> {
  const accountInfo = await connection.getAccountInfo(greetedPubkey);
  if (accountInfo === null) {
    throw 'Error: cannot find the greeted account';
  }
  //const info = greetedAccountDataLayout.decode(Buffer.from(accountInfo.data));
  /*
  console.log(
    greetedPubkey.toBase58(),
    'has been greeted',
    info.numGreets.toString(),
    'times',
  );
  */
 //console.log(greetedPubkey.toBase58(), ":");
 //console.log("Account data:", info.account_data.toString());
}

/**
 * Print given accounts' data
 */
async function printAccountData(account: PublicKey): Promise<void> {
  const accountInfo = await connection.getAccountInfo(greetedPubkey);
  if (accountInfo === null) {
    throw 'Error: cannot get data for account ' + account.toBase58();
  }
  //const info = accountLayout.decode(Buffer.from(accountInfo.data));
  //console.log("Account has", accountInfo.data.length, "bytes");
  printAccountPosts(accountInfo.data);
}

/**
 * Report the accounts owned by the program
 */
export async function reportAccounts(): Promise<void> {
  const accounts = await connection.getProgramAccounts(programId);
  console.log("Accounts owned by program:");
  for(let i = 0; i < accounts.length; i++) {
    console.log(accounts[i].pubkey.toBase58());
    await printAccountData(accounts[i].pubkey);
  }
}