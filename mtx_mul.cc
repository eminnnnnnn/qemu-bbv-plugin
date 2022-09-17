#include <cstdlib>
#include <ctime>

int main(int argc, char** argv) {
	if (argc < 2) return -1;

	int mtxSize = std::atoi(argv[1]);
	int mtxDim = mtxSize * mtxSize;

	int* mtxA = new int[mtxDim];
	int* mtxB = new int[mtxDim];
	int* mtxC = new int[mtxDim];

	std::srand(std::time(nullptr));

	for (int i = 0; i < mtxDim; i++) {
		mtxA[i] = std::rand() % 100;
		mtxB[i] = std::rand() % 100;
		mtxC[i] = 0;
	}

	for (int i = 0; i < mtxSize; i++) {
		for (int j = 0; j < mtxSize; j++) {
			for (int k = 0; k < mtxSize; k++) {
				mtxC[i*mtxSize + j] += mtxA[i*mtxSize + k] * mtxB[k*mtxSize + j];
			}
		}
	}

	delete[] mtxA;
	delete[] mtxB;
	delete[] mtxC;
	return 0;
}
