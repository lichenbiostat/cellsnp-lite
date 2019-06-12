# Utilility functions for processing vcf files
# Author: Yuanhua Huang
# Date: 09/06/2019

import os
import sys
import gzip
import h5py
import subprocess
import numpy as np

def parse_sample_info(sample_dat, sparse=True):
    """
    Parse genotype information for each sample
    Note, it requires the format for each variants to 
    be the same.
    """
    if sample_dat == [] or sample_dat is None:
        return None

    # require the same format for all variants
    format_all = [x[0] for x in sample_dat]
    if format_all.count(format_all[0]) != len(format_all):
        print("Error: require the same format for all variants.")
        exit()
    format_list = format_all[0].split(":")
    
    RV = {}
    for _format in format_list:
        RV[_format] = []
    if sparse:
        RV['indices'] = []
        RV['indptr'] = [0]
        RV['shape'] = (len(sample_dat[0][1:]), len(sample_dat))
        missing_val = ":".join(["."] * len(format_list))
        
        cnt = 0
        for j in range(len(sample_dat)): #variant j
            _line = sample_dat[j]
            for i in range(len(_line[1:])): #cell i
                if _line[i+1] == missing_val:
                    continue
                _line_key = _line[i+1].split(":")
                for k in range(len(format_list)):
                    RV[format_list[k]].append(_line_key[k])

                cnt += 1
                RV['indices'].append(i)
            RV['indptr'].append(cnt)
    else:
        for _line in sample_dat:
            _line_split = [x.split(":") for x in _line[1:]]
            for k in range(len(format_list)):
                _line_key = [x[k] for x in _line_split]
                RV[format_list[k]].append(_line_key)
    return RV


def load_VCF(vcf_file, biallelic_only=False, load_sample=True, sparse=True):
    """
    Load whole VCF file 
    -------------------
    Initially designed to load VCF from cellSNP output, requiring 
    1) all variants have the same format list;
    2) a line starting with "#CHROM", with sample ids.
    If these two requirements are satisfied, this function also supports general
    VCF files, e.g., genotype for multiple samples.

    Note, it may take a large memory, please filter the VCF with bcftools first.
    """
    if vcf_file[-3:] == ".gz" or vcf_file[-4:] == ".bgz":
        infile = gzip.open(vcf_file, "rb")
        is_gzip = True
    else:
        infile = open(vcf_file, "r")
        is_gzip = False
    
    FixedINFO = {}
    contig_lines = []
    comment_lines = []
    var_ids, obs_ids, obs_dat = [], [], []
    
    for line in infile:
        if is_gzip:
            line = line.decode('utf-8')
        if line.startswith("#"):
            if line.startswith("##contig="):
                contig_lines.append(line.rstrip())
            if line.startswith("#CHROM"):
                obs_ids = line.rstrip().split("\t")[9:]
                key_ids = line[1:].rstrip().split("\t")[:8]
                for _key in key_ids:
                    FixedINFO[_key] = []
            else:
                comment_lines.append(line.rstrip())
        else:
            list_val = line.rstrip().split("\t") #[:5] #:8
            if biallelic_only:
                if len(list_val[3]) > 1 or len(list_val[4]) > 1:
                    continue
            if load_sample:
                obs_dat.append(list_val[8:])
            for i in range(len(key_ids)):
                FixedINFO[key_ids[i]].append(list_val[i])
            var_ids.append("_".join([list_val[x] for x in [0, 1, 3, 4]]))
    infile.close()

    RV = {}
    RV["variants"]  = var_ids
    RV["FixedINFO"] = FixedINFO
    RV["samples"]   = obs_ids
    RV["GenoINFO"]  = parse_sample_info(obs_dat, sparse=sparse)
    RV["contigs"]   = contig_lines
    RV["comments"]  = comment_lines
    return RV


def write_VCF_to_hdf5(VCF_dat, out_file):
    """
    Write vcf data into hdf5 file
    """
    f = h5py.File(out_file, 'w')
    f.create_dataset("contigs", data=np.string_(VCF_dat['contigs']), 
                     compression="gzip", compression_opts=9)
    f.create_dataset("samples", data=np.string_(VCF_dat['samples']), 
                     compression="gzip", compression_opts=9)
    f.create_dataset("variants", data=np.string_(VCF_dat['variants']), 
                     compression="gzip", compression_opts=9)
    f.create_dataset("comments", data=np.string_(VCF_dat['comments']), 
                     compression="gzip", compression_opts=9)
    
    ## variant fixed information
    fixed = f.create_group("FixedINFO")
    for _key in VCF_dat['FixedINFO']:
        fixed.create_dataset(_key, data=np.string_(VCF_dat['FixedINFO'][_key]), 
                             compression="gzip", compression_opts=9)
        
    ## genotype information for each sample
    geno = f.create_group("GenoINFO")
    for _key in VCF_dat['GenoINFO']:
        geno.create_dataset(_key, data=np.string_(VCF_dat['GenoINFO'][_key]), 
                             compression="gzip", compression_opts=9)
        
    f.close()


def read_sparse_GeneINFO(GenoINFO, keys=['AD', 'DP']):
    M, N = np.array(GenoINFO['shape']).astype('int')
    indptr = np.array(GenoINFO['indptr']).astype('int')
    indices = np.array(GenoINFO['indices']).astype('int')
    
    from scipy.sparse import csr_matrix
    
    RV = {}
    for _key in keys:
        data = np.array(GenoINFO[_key]).astype('float')
        RV[_key] = csr_matrix((data, indices, indptr), shape=(N, M))
    return RV


def merge_vcf(out_file, out_files, hdf5_out=True):
    """Merge vcf for all chromsomes
    """
    if out_file.endswith(".gz"):
        out_file_use = out_file.split(".gz")[0]
    else:
        out_file_use = out_file
        
    CNT = 0
    fid_out = open(out_file_use, "w")
    for _file in out_files:
        with open(_file, "r") as fid_in:
            for line in fid_in:
                if line.startswith("#") and _file != out_files[0]:
                    continue
                else:
                    CNT += 1
                    fid_out.writelines(line)
        os.remove(_file)
    fid_out.close()
    print("[cellSNP] %d lines in final vcf file" %CNT)
    
    bashCommand = "gzip -f %s" %(out_file_use)
    pro = subprocess.Popen(bashCommand.split(), stdout=subprocess.PIPE)
    pro.communicate()[0]

    ## save to hdf5 file
    if hdf5_out:
        vcf_dat = load_VCF(out_file_use + ".gz", load_sample=True, sparse=True)
        write_VCF_to_hdf5(vcf_dat, out_file_use + ".h5")
    
    return None
