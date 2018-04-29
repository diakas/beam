#include "receiver.h"

namespace // TODO: make a separate function for random
{
    void GenerateRandom(void* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            ((uint8_t*) p)[i] = (uint8_t) rand();
    }

    void SetRandom(ECC::uintBig& x)
    {
        GenerateRandom(x.m_pData, sizeof(x.m_pData));
    }

    void SetRandom(ECC::Scalar::Native& x)
    {
        ECC::Scalar s;
        while (true)
        {
            SetRandom(s.m_Value);
            if (!x.Import(s))
                break;
        }
    }
}

namespace beam::wallet
{
    void Receiver::FSMDefinition::confirmTx(const msmf::none&)
    {
        auto confirmationData = std::make_shared<receiver::ConfirmationData>();
        confirmationData->m_txId = m_txId;

        TxKernel::Ptr kernel = std::make_unique<TxKernel>();
        kernel->m_Fee = 0;
        kernel->m_HeightMin = 0;
        kernel->m_HeightMax = static_cast<Height>(-1);
        m_kernel = kernel.get();
        m_transaction->m_vKernels.push_back(std::move(kernel));

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        ECC::Amount amount = m_amount;
        Output::Ptr output = std::make_unique<Output>();
        output->m_Coinbase = false;

        ECC::Scalar::Native blindingFactor;
        SetRandom(blindingFactor);
        ECC::Point::Native pt;
        pt = ECC::Commitment(blindingFactor, amount);
        output->m_Commitment = pt;

        output->m_pPublic.reset(new ECC::RangeProof::Public);
        output->m_pPublic->m_Value = amount;
        output->m_pPublic->Create(blindingFactor);

        blindingFactor = -blindingFactor;
        m_blindingExcess += blindingFactor;

        m_transaction->m_vOutputs.push_back(std::move(output));

        // 4. Calculate message M
        // 5. Choose random nonce
        ECC::Signature::MultiSig msig;
        SetRandom(m_nonce);
        //msig.GenerateNonce(m_message, m_blindingExcess);
        //m_nonce = msig.m_Nonce;
        msig.m_Nonce = m_nonce;
        // 6. Make public nonce and blinding factor
        m_publicReceiverBlindingExcess 
            = confirmationData->m_publicReceiverBlindingExcess 
            = ECC::Context::get().G * m_blindingExcess;

        confirmationData->m_publicReceiverNonce = ECC::Context::get().G * m_nonce;
        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_publicSenderNonce + confirmationData->m_publicReceiverNonce;
        // 8. Compute recepient Shnorr signature
        m_kernel->m_Signature.CoSign(m_receiverSignature, m_message, m_blindingExcess, msig);
        
        confirmationData->m_receiverSignature = m_receiverSignature;

        m_gateway.sendTxConfirmation(confirmationData);
    }

    bool Receiver::FSMDefinition::isValidSignature(const TxConfirmationCompleted& event)
    {
        auto data = event.data;
        // 1. Verify sender's Schnor signature
        ECC::Scalar::Native ne = m_kernel->m_Signature.m_e;
        ne = -ne;
        ECC::Point::Native s, s2;

        s = m_publicSenderNonce;
        s += m_publicSenderBlindingExcess * ne;

        s2 = ECC::Context::get().G * data->m_senderSignature;
        ECC::Point p(s), p2(s2);

        return (p == p2);
    }

    bool Receiver::FSMDefinition::isInvalidSignature(const TxConfirmationCompleted& event)
    {
        return !isValidSignature(event);
    }

    void Receiver::FSMDefinition::registerTx(const TxConfirmationCompleted& event)
    {
        // 2. Calculate final signature
        ECC::Scalar::Native finialSignature = event.data->m_senderSignature + m_receiverSignature;

        // 3. Calculate public key for excess
        ECC::Point::Native x = m_publicReceiverBlindingExcess;
        x += m_publicSenderBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_kernel->m_Excess = x;
        m_kernel->m_Signature.m_k = finialSignature;      

        // 6. Create final transaction and send it to mempool
        ECC::Amount fee = 0U;
        
        // TODO: uncomment assert
       // assert(m_transaction->IsValid(fee, 0U));
        m_gateway.registerTx(m_txId, m_transaction);
    }

    void Receiver::FSMDefinition::rollbackTx(const TxFailed& event)
    {
        std::cout << "Receiver::rollbackTx\n";
    }

    void Receiver::FSMDefinition::cancelTx(const TxConfirmationCompleted& )
    {
        std::cout << "Receiver::cancelTx\n";
    }

    void Receiver::FSMDefinition::confirmOutput(const TxRegistrationCompleted& )
    {
        m_gateway.sendTxRegistered(m_txId);
    }

    void Receiver::FSMDefinition::completeTx(const TxOutputConfirmCompleted& )
    {
        std::cout << "Receiver::completeTx\n";
    }
}