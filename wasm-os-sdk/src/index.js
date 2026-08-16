const protocol = require("./protocol");
const { createDeviceClient, hardReset, RESPONSE_TIMEOUT, PUSH_BEGIN_TIMEOUT } = require("./client");
const nodeSerial = require("./transports/node-serial");
const webSerial = require("./transports/web-serial");

module.exports = {
  ...protocol,
  createDeviceClient,
  hardReset,
  RESPONSE_TIMEOUT,
  PUSH_BEGIN_TIMEOUT,
  ...nodeSerial,
  openWebSerialTransport: webSerial.openWebSerialTransport,
};
