// qr.h - tiny QR code encoder used to hand the address to a phone camera.
#pragma once
#include <vector>
#include <string>

// Encode `text` as a QR symbol in byte mode, error-correction level M.
// Returns a size*size row-major bitmap (true = dark module). Empty on failure.
std::vector<unsigned char> qr_encode(const std::string& text, int* outSize);
