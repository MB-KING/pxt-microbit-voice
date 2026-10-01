/** Experimental native voice transport helpers. Hardware V2 only. */
//% color=#006D77 icon="\uf130"
namespace voice {
    //% shim=voice::setup
    export function setup(): void {}
    //% shim=voice::startCapture
    export function startCapture(): void {}
    //% shim=voice::stopCapture
    export function stopCapture(): void {}
    //% shim=voice::nextPacket
    export function nextPacket(sender: number): Buffer { return pins.createBuffer(0) }
    //% shim=voice::receivePacket
    export function receivePacket(packet: Buffer): void {}
    //% shim=voice::playPending
    export function playPending(): void { basic.pause(1) }
}
