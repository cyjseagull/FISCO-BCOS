// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

abstract contract CipherMatchPrecompiled
{
    function bls128CipherMatch(bytes memory cipher, bytes memory trapdoor) public virtual view returns(bool);
}