    // Cleanup
    cudaFree(d_expW);
    cudaFree(d_expSc);
    cudaFree(d_expIdx);
    cudaFree(d_expVal);
    cudaFree(d_prjW);
    cudaFree(d_prjSc);
    cudaFree(d_prjIdx);
    cudaFree(d_prjVal);
    cudaFree(d_expFp16);
    cudaFree(d_prjFp16);
    cudaFree(d_expFp8);
    cudaFree(d_prjFp8);
    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_intermediate);

    std::printf("========================================================================================\n");
    std::printf("Benchmark completed successfully on physical NVIDIA Ada SM89 GPU!\n");
    std::printf("========================================================================================\n");

    return 0;
}
