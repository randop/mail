import * as flatbuffers from "flatbuffers";

import { MyGame } from "./monster_generated";

let data = new Uint8Array(fs.readFileSync("monster.dat"));
let buf = new flatbuffers.ByteBuffer(data);
let monster = MyGame.Example.Monster.getRootAsMonster(buf);

import * as fs from "fs";
import * as flatbuffers from "flatbuffers";
import { MyGame } from "./monster_generated"; // Adjust path if needed

// Read the file as a UTF-8 string (assuming it contains a plain Base64-encoded string)
const base64String: string = fs
	.readFileSync("monster_base64.txt", "utf8")
	.trim();

// Decode the Base64 string directly to Uint8Array (Buffer extends Uint8Array in Node.js)
const data: Uint8Array = Buffer.from(base64String, "base64");

// Create the FlatBuffers ByteBuffer and load the Monster
const buf = new flatbuffers.ByteBuffer(data);
const monster = MyGame.Example.Monster.getRootAsMonster(buf);

// Now you can access the monster data
console.log(monster.hp()); // Example access
