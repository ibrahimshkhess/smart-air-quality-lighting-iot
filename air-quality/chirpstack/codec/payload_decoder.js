/**
 * Decode uplink function
 * 
 * @param {object} input
 * @param {number[]} input.bytes Byte array containing the uplink payload, e.g. [255, 230, 255, 0]
 * @param {number} input.fPort Uplink fPort.
 * @param {Record<string, string>} input.variables Object containing the configured device variables.
 * 
 * @returns {{data: object, errors?: string[], warnings?: string[]}}
 * An object containing:
 * - data: Object representing the decoded payload.
 * - errors: An array of errors (optional).
 * - warnings: An array of warnings (optional).
 */
function decodeUplink(input) {
  var b = input.bytes;

  if (b.length !== 18) {
    return {
      data: {},
      errors: ["Invalid payload length: expected 18 bytes, got " + b.length]
    };
  }

  var version = b[0];
  var nodeId = b[1];

  var seq =
    ((b[2] << 24) >>> 0) |
    (b[3] << 16) |
    (b[4] << 8) |
    b[5];

  var timestamp =
    ((b[6] << 24) >>> 0) |
    (b[7] << 16) |
    (b[8] << 8) |
    b[9];

  var gas =
    (b[10] << 8) |
    b[11];

  var pm1 =
    (b[12] << 8) |
    b[13];

  var pm25 =
    (b[14] << 8) |
    b[15];

  var pm10 =
    (b[16] << 8) |
    b[17];

  var node;

  if (nodeId === 1) {
    node = "S1";
  } else if (nodeId === 2) {
    node = "S2";
  } else {
    node = "UNKNOWN";
  }

  return {
    data: {
      version: version,
      node: node,
      node_id: nodeId,
      seq: seq,
      ts_ms: timestamp,
      gas: gas,
      pm1: pm1,
      pm25: pm25,
      pm10: pm10
    }
  };
}
/**
 * Encode downlink function.
 * 
 * @param {object} input
 * @param {object} input.data Object representing the payload that must be encoded.
 * @param {Record<string, string>} input.variables Object containing the configured device variables.
 * 
 * @returns {{bytes: number[], fPort?: number, errors?: string[], warnings?: string[]}}
 * An object containing:
 * - bytes: Byte array containing the downlink payload.
 * - fPort: The downlink LoRaWAN fPort. (falls back to provided fPort)
 * - errors: An array of errors (optional).
 * - warnings: An array of warnings (optional).
 */
function encodeDownlink(input) {
  return {
    fPort: 10,
    bytes: [225, 230, 255, 0],
  };
}
