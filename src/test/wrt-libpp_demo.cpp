//------------------------------------------------------------------------------
/*
    This file is part of wrt-libpp: https://github.com/World-of-Retail-Token/wrt-libpp
    Copyright (c) 2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/STTx.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/to_string.h>
#include <algorithm>

std::string serialize(ripple::STTx const& tx)
{
    using namespace ripple;

    return strHex(tx.getSerializer().peekData());
}

std::shared_ptr<ripple::STTx const> deserialize(std::string blob)
{
    using namespace ripple;

    auto ret{ strUnHex(blob) };

    if (!ret || !ret->size())
        Throw<std::runtime_error>("transaction not valid hex");

    SerialIter sitTrans{ makeSlice(*ret) };
    // Can Throw
    return std::make_shared<STTx const>(std::ref(sitTrans));
}

//------------------------------------------------------------------------------

bool demonstrateSigning(std::string seedStr, std::string expectedAccount)
{
    using namespace ripple;

    auto const seed = parseGenericSeed(seedStr);
    assert(seed);
    auto const keypair = generateKeyPair(*seed, true);
    auto const id = calcAccountID(keypair.first);
    assert(toBase58(id) == expectedAccount);

    std::cout << "\n secret \"" << seedStr
        << "\" generates secret key \"" << toBase58(*seed)
        << "\" and public key \"" << toBase58(id) << "\"\n";

    auto const destination = parseBase58<AccountID>(
        "19HVVcvqHvFocvwpdN4nB5AXXgg19qqpw3");
    assert(destination);
    auto const gateway1 = parseBase58<AccountID>(
        "1BkdSQgVE1gyf7HZEJh3qo777MsKTC1eWJ");
    assert(gateway1);
    auto const gateway2 = parseBase58<AccountID>(
        "19NfziqriXz3UnM3wARPTT3jx6eV1bbPmh");
    assert(gateway2);
    STTx noopTx(ttPAYMENT,
        [&](auto& obj)
    {
        // General transaction fields
        obj[sfAccount] = id;
        obj[sfFee] = STAmount{ 100 };
        obj[sfFlags] = tfFullyCanonicalSig;
        obj[sfSequence] = 18;
        obj[sfSigningPubKey] = keypair.first.slice();
        // Payment-specific fields
        obj[sfAmount] = STAmount(Issue(to_currency("USD"), *gateway1),
            1234, 5);
        obj[sfDestination] = *destination;
        obj[sfSendMax] = STAmount(Issue(to_currency("RUB"), *gateway2),
            56789, 7);
    });

    std::cout << "\nBefore signing: \n" <<
        noopTx.getJson(JsonOptions::none).toStyledString() << "\n" <<
        "Serialized: " << noopTx.getJson(JsonOptions::none, true)[jss::tx] << "\n";

    noopTx.sign(keypair.first, keypair.second);

    auto const serialized = serialize(noopTx);
    std::cout << "\nAfter signing: \n" <<
        noopTx.getJson(JsonOptions::none).toStyledString() << "\n" <<
        "Serialized: " << serialized << "\n";

    auto const deserialized = deserialize(serialized);
    assert(deserialized);
    assert(deserialized->getTransactionID() == noopTx.getTransactionID());
    std::cout << "Deserialized: " <<
        deserialized->getJson(JsonOptions::none).toStyledString() << "\n";

    auto const check1 = noopTx.checkSign(false);

    std::cout << "Check 1: " << (check1.first ? "Good" : "Bad!") << "\n";
    assert(check1.first);

    // Use the function primitives, which are hidden by the `STTx`
    // interface, to check the signature again and verify that
    // `STTx` is working properly.
    auto const& signatureSlice = noopTx[sfTxnSignature];
    Blob const data = [&]
    {
        // This is a copy of the static `getSigningData` function body
        // which is needed by `verify`.
        Serializer s;
        s.add32(HashPrefix::txSign);
        noopTx.addWithoutSigningFields(s);
        return s.getData();
    }();

    // STTx::checkSign calls `verify` indirectly via `checkSingleSign`
    auto const check2 = verify(
        keypair.first,
        makeSlice(data),
        signatureSlice,
        true);

    std::cout << "Check 2: " << (check2 ? "Good" : "Bad!") << "\n";

    return check1.first && check2;
}

bool exerciseSingleSign ()
{
    std::vector<bool> passes;

    // Genesis account w/ not-so-secret key.
    // Never hardcode a real secret key.
    passes.emplace_back(demonstrateSigning("35tDMF67PWq8XmqY88CjBPZspfwX2", "19HVVcvqHvFocvwpdN4nB5AXXgg19qqpw3"));

    {
        passes.emplace_back(false);
        try
        {
            auto const empty = deserialize("");
        }
        catch (std::runtime_error e)
        {
            passes.back() = true;
            assert(e.what() == std::string("transaction not valid hex"));
        }
    }

    auto const allPass = std::all_of(passes.begin(), passes.end(),
        [](auto p) { return p; });
    assert(allPass);
    std::cout << (allPass ?
        "All single signing checks pass.\n" :
        "Some single signing checks fail.\n");

    return allPass;
}

//------------------------------------------------------------------------------

// Demonstrate multisigning.

// Helper function that asserts if for some reason we can't create a seed.
ripple::Seed getSeed (std::string const& seedText)
{
    // WARNING!
    // Never use ripple::parseGenericSeed() for secure code.  Call
    // ripple::randomSeed() instead, since it is cryptographically secure.
    boost::optional<ripple::Seed> const possibleSeed {
        ripple::parseGenericSeed (seedText)};

    // Should not be necessary in production code, since you used
    // ripple::randomSeed().  Right?
    assert (possibleSeed);

    return *possibleSeed;
}

// Holds identifying information for an account.
class Credentials
{
    std::string const name_;
    ripple::Seed const seed_;
    std::pair<ripple::PublicKey, ripple::SecretKey> const keys_;
    ripple::AccountID const id_;

public:
    Credentials (std::string name)
    : name_ (name)
    , seed_ (getSeed (name_))
    , keys_ (ripple::generateKeyPair (seed_, true))
    , id_ (ripple::calcAccountID (keys_.first))
    {
    }

    std::string const& name() const { return name_; }
    ripple::Seed const& seed() const { return seed_; }
    ripple::SecretKey const& secretKey() const { return keys_.second; }
    ripple::PublicKey const& publicKey() const { return keys_.first; }
    ripple::AccountID const& id() const { return id_; }
};

// Build a transaction that can be multisigned.  All fields must be filled in,
// including sequence and fee, before any signatures are applied.  If the
// contents of the transaction are modified then any previously provided
// multi-signatures will become invalid.
ripple::STTx buildMultisignTx (
    ripple::AccountID const& id, std::uint32_t seq, std::uint32_t fee)
{
    using namespace ripple;

    STTx noopTx {ttACCOUNT_SET,
        [id, seq, fee] (auto& obj)
    {
        obj[sfAccount] = id;
        obj[sfFlags] = tfFullyCanonicalSig;
        obj[sfFee] = STAmount {fee};               // Must be already filled in
        obj[sfSequence] = seq;                     // Must be already filled in
        obj[sfSigningPubKey] = Slice {nullptr, 0}; // Must be present and empty
    }};

    std::cout << "\nBefore signing: \n"
        << noopTx.getJson(JsonOptions::none, false).toStyledString()  << std::endl;

    return noopTx;
}

// Apply one multi-signature to the supplied transaction.  The signer
// provides their AccountID, PublicKey, and SecretKey.
bool multisign (ripple::STTx& tx, Credentials const& signer)
{
    using namespace ripple;

    // Get the TxnSignature.
    Serializer s = buildMultiSigningData (tx, signer.id());

    auto const multisig = ripple::sign (
        signer.publicKey(), signer.secretKey(), s.slice());

    // Make the signer object that we'll inject into the array.
    STObject element (sfSigner);
    element[sfAccount] = signer.id();
    element[sfSigningPubKey] = signer.publicKey();
    element[sfTxnSignature] = multisig;

    // If a Signers array does not yet exist make one.
    if (! tx.isFieldPresent (sfSigners))
        tx.setFieldArray (sfSigners, {});

    // Insert the signer into the array.
    STArray& signers {tx.peekFieldArray (sfSigners)};
    signers.emplace_back (std::move (element));

    // Sort the Signers array by Account.  If it is not sorted when submitted
    // to the network then it will be rejected.
    std::sort (signers.begin(), signers.end(),
        [](STObject const& a, STObject const& b)
    {
        return (a[sfAccount] < b[sfAccount]);
    });

    // Verify that the signature is valid.
    bool const pass = tx.checkSign(true).first;
    assert (pass);

    // To submit multisigned JSON to the network use this RPC command:
    // $ rippled submit_multisigned '<all JSON>'
    std::cout << "\nMultisigned JSON: \n"
        << tx.getJson(JsonOptions::none, false).toStyledString()  << std::endl;

    // Alternatively, to submit the multisigned blob to the network:
    //  1. Extract the hex string (including the quotes) following "tx"
    //  2. Then use this RPC command:
    //     $ rippled submit <quoted hex string>
    std::cout << "Multisigned blob:"
        << tx.getJson(JsonOptions::none, true) << std::endl;

    return pass;
}

// Outlines a multisign process where:
//  1. A multi-signable transaction is built.
//  2. That transaction is signed by one signer.
//  3. The transaction is signed by a different signer.
bool exerciseMultiSign()
{
    using namespace ripple;

    // Create credentials for the folks involved in the transaction.
    Credentials const alice {"alice"};
    Credentials const billy {"billy"};
    Credentials const carol {"carol"};

    // Create a transaction on alice's account.  alice doesn't sign it.
    STTx tx {buildMultisignTx (alice.id(), 2, 100)};

    // billy and carol sign alice's transaction for her.
    bool allPass = multisign (tx, billy);
    allPass &= multisign (tx, carol);

    return allPass;
}

//------------------------------------------------------------------------------

int main (int argc, char** argv)
{
    // Display the version
    std::cout << "ripple-libpp_demo built with ripple core version " <<
        ripple::BuildInfo::getVersionString() << "\n";

    // Demonstrate single signing.
    auto allPass = exerciseSingleSign();

    // Demonstrate multisigning.
    allPass &= exerciseMultiSign();

    assert(allPass);
    std::cout << (allPass ?
        "All checks pass.\n" : "Some checks fail.\n");

    return allPass ? 0 : 1;
}

