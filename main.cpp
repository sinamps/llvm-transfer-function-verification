// Comparing and testing the composed and decomposed versions of the sextInReg transfer function from LLVM KnownBits class

#include <llvm/ADT/APInt.h>
#include <llvm/Support/KnownBits.h>
#include <vector>
#include <set>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cassert>

using namespace llvm;

// Custom comparator for APInt to enable < for std::set
struct APIntComparator {
    bool operator()(const llvm::APInt &lhs, const llvm::APInt &rhs) const {
        return lhs.ult(rhs); // unsigned less than
    }
};

// Helper function to count bits set to one in an APInt
unsigned countSetBits(const APInt &Value) {
    unsigned count = 0;
    unsigned BitWidth = Value.getBitWidth();
    for (unsigned i = 0; i < BitWidth; ++i) {
        if (Value[i]) {
            ++count;
        }
    }
    return count;
}

// Function to enumerate all possible KnownBits values for a given bitwidth
void enumerateKnownBits(unsigned BitWidth, std::vector<KnownBits> &KnownBitsList) {
    uint64_t NumAbstractValues = pow(3, BitWidth); // 3^BitWidth possible abstract values
    KnownBitsList.reserve(NumAbstractValues);

    // Each bit can be 0 (known zero), 1 (known one), or X (unknown)
    // Represent each abstract value as a number in base 3
    for (uint64_t i = 0; i < NumAbstractValues; ++i) {
        KnownBits KBInstance(BitWidth);
        uint64_t AbsVal = i;
        bool Valid = true;
        for (unsigned Bit = 0; Bit < BitWidth; ++Bit) {
            unsigned Rem = AbsVal % 3;
            if (Rem == 0) {
                KBInstance.Zero.setBit(Bit);
            } else if (Rem == 1) {
                KBInstance.One.setBit(Bit);
            } else if (Rem == 2) {
                // Unknown bit, do nothing
            }
            AbsVal = AbsVal / 3;
        }
        if (Valid)
            KnownBitsList.push_back(KBInstance);
    }
}

// Function to concretize a KnownBits value to a set of APInt values
void concretize(const KnownBits &KBInstance, std::set<APInt, APIntComparator> &ConcreteValues) {
    unsigned BitWidth = KBInstance.getBitWidth();
    unsigned NumKnownZeroBits = countSetBits(KBInstance.Zero);
    unsigned NumKnownOneBits = countSetBits(KBInstance.One);
    unsigned NumUnknownBits = BitWidth - (NumKnownZeroBits + NumKnownOneBits);
    uint64_t NumConcreteValues = pow(2, NumUnknownBits);
    ConcreteValues.clear();

    // Positions of unknown bits
    std::vector<unsigned> UnknownBitPositions;
    for (unsigned Bit = 0; Bit < BitWidth; ++Bit) {
        if (!KBInstance.Zero[Bit] && !KBInstance.One[Bit])
            UnknownBitPositions.push_back(Bit);
    }

    // Generate all combinations of unknown bits
    for (uint64_t i = 0; i < NumConcreteValues; ++i) {
        APInt Value = KBInstance.One;
        for (unsigned j = 0; j < UnknownBitPositions.size(); ++j) {
            unsigned BitPosition = UnknownBitPositions[j];
            if (i & (1ULL << j))
                Value.setBit(BitPosition);
            else
                Value.clearBit(BitPosition);
        }
        ConcreteValues.insert(Value);
    }
}

// Function to abstract a set of APInt values to a KnownBits value
KnownBits abstract(const std::set<APInt, APIntComparator> &Values, unsigned BitWidth) {
    KnownBits KBInstance(BitWidth);
    if (Values.empty())
        return KBInstance;

    // Initialize KnownZero and KnownOne
    APInt KnownZero = ~Values.begin()->zextOrTrunc(BitWidth);
    APInt KnownOne = Values.begin()->zextOrTrunc(BitWidth);

    for (const auto &Val : Values) {
        KnownZero &= ~Val;
        KnownOne &= Val;
    }

    KBInstance.Zero = KnownZero;
    KBInstance.One = KnownOne;
    return KBInstance;
}


// Composite transfer function for sextInReg from LLVM KnownBits class
KnownBits sextInRegComposite(const KnownBits &KBInstance, unsigned SrcBitWidth) {
    unsigned BitWidth = KBInstance.getBitWidth();
    assert(0 < SrcBitWidth && SrcBitWidth <= BitWidth && "Illegal sext-in-register");

    if (SrcBitWidth == BitWidth)
        return KBInstance;
    
    unsigned ExtBits = BitWidth - SrcBitWidth;
    KnownBits Result;
    Result.One = KBInstance.One << ExtBits;
    Result.Zero = KBInstance.Zero << ExtBits;
    Result.One.ashrInPlace(ExtBits);
    Result.Zero.ashrInPlace(ExtBits);
    return Result;
}


// Decomposed transfer function using simpler operations
KnownBits sextInRegDecomposed(const KnownBits &KBInstance, unsigned SrcBitWidth) {
    unsigned BitWidth = KBInstance.getBitWidth();
    assert(0 < SrcBitWidth && SrcBitWidth <= BitWidth && "Illegal sext-in-register");

    if (SrcBitWidth == BitWidth)
        return KBInstance;

    unsigned ExtBits = BitWidth - SrcBitWidth;
    KnownBits Result(BitWidth);

    // Copy the original known bits into the lower SrcBitWidth bits
    for (unsigned i = 0; i < SrcBitWidth; ++i) {
        if (KBInstance.One[i])
            Result.One.setBit(i);
        if (KBInstance.Zero[i])
            Result.Zero.setBit(i);
    }

    // Determine the sign bit
    unsigned SignBitIndex = SrcBitWidth - 1;
    bool SignBitKnownOne = KBInstance.One[SignBitIndex];
    bool SignBitKnownZero = KBInstance.Zero[SignBitIndex];

    // Extend the sign bit into the higher bits
    for (unsigned i = SrcBitWidth; i < BitWidth; ++i) {
        if (SignBitKnownOne)
            Result.One.setBit(i);
        else if (SignBitKnownZero)
            Result.Zero.setBit(i);
        else {
            // Sign bit is unknown, so higher bits are unknown
            // No need to set anything since bits are initialized to unknown
            break;
        }
    }

    return Result;
}


// Function to compare the composite and decomposed transfer functions
void testTransferFunctions(unsigned BitWidth, unsigned SrcBitWidth) {
    std::vector<KnownBits> KnownBitsList;
    enumerateKnownBits(BitWidth, KnownBitsList);

    uint64_t TotalComparisons = 0;
    uint64_t CompositeMorePrecise = 0;
    uint64_t DecomposedMorePrecise = 0;
    uint64_t EquallyPrecise = 0;    // Added counter for equally precise cases
    uint64_t Incomparable = 0;

    for (const auto &KBInstance : KnownBitsList) {
        TotalComparisons++;

        KnownBits CompositeResult = sextInRegComposite(KBInstance, SrcBitWidth);
        KnownBits DecomposedResult = sextInRegDecomposed(KBInstance, SrcBitWidth);

        // Check which result is more precise
        std::set<APInt, APIntComparator> CompositeConcrete, DecomposedConcrete;
        concretize(CompositeResult, CompositeConcrete);
        concretize(DecomposedResult, DecomposedConcrete);

        bool CompositeSubset = std::includes(DecomposedConcrete.begin(), DecomposedConcrete.end(),
                                             CompositeConcrete.begin(), CompositeConcrete.end(), APIntComparator());

        bool DecomposedSubset = std::includes(CompositeConcrete.begin(), CompositeConcrete.end(),
                                              DecomposedConcrete.begin(), DecomposedConcrete.end(), APIntComparator());

        if (CompositeConcrete == DecomposedConcrete) {
            EquallyPrecise++;
        } else if (CompositeSubset && !DecomposedSubset) {
            CompositeMorePrecise++;
        } else if (DecomposedSubset && !CompositeSubset) {
            DecomposedMorePrecise++;
        } else {
            Incomparable++;
        }
    }

    std::cout << "BitWidth: " << BitWidth << ", SrcBitWidth: " << SrcBitWidth << "\n";
    std::cout << "Total Values: " << TotalComparisons << "\n";
    std::cout << "Equal Precision: " << EquallyPrecise << "\n";
    std::cout << "Composite More Precise: " << CompositeMorePrecise << "\n";
    std::cout << "Decomposed More Precise: " << DecomposedMorePrecise << "\n";
    std::cout << "Incomparable Results: " << Incomparable << "\n\n";
}

// Function to run tests for bit widths from 4 to 8
void runTests() {
    for (unsigned BitWidth = 4; BitWidth <= 8; ++BitWidth) {
        for (unsigned SrcBitWidth = 1; SrcBitWidth <= BitWidth; ++SrcBitWidth) {
            testTransferFunctions(BitWidth, SrcBitWidth);
        }
    }
}

int main() {
    runTests();
    return 0;
}
