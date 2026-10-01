/** Experimental native voice transport. Hardware V2 only. */
//% color=#006D77 icon="\uf130"
namespace voice {
    //% shim=voice::setup
    export function setup(): void {}
    //% shim=voice::setDevice
    export function setDevice(id: number): void {}
    //% shim=voice::startCapture
    export function startCapture(): void {}
    //% shim=voice::stopCapture
    export function stopCapture(): void {}
}
