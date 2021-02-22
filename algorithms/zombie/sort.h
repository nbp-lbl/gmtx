#ifndef SORT_H
#define SORT_H

#define SORT_COLS 16

/**
 * Sort elements of a gmt_array according to columns in ascending order.
 * Returns a new gmt_array.
*/ 
gmt_data_t gmt_sort(gmt_data_t data, uint64_t * columns);

/*
 * Return index of first element of gmt array NOT less than value when compared according to columns.
 * Returns 0 if value is less than first element of gmt array.
 * Return number of elements if value is greater than last element of gmt array
*/
uint64_t gmt_lower_bound(std::vector <uint64_t> & value, gmt_data_t data,  uint64_t * columns);

/*
 * Return index of first element of gmt array greater than value when compared according to columns.
 * Return number of elements if value is greater than last element of gmt array
*/
uint64_t gmt_upper_bound(std::vector <uint64_t> & value, gmt_data_t data,  uint64_t * columns);

/*
 * Return the range of elements of gmt array that are equal to value when compared according to columns.
 * If no element is equal to value, range elements equal index of first row greater than value.  If all
 * elements are less than value, range elements equal number of elements.
*/
std::pair <uint64_t, uint64_t> gmt_equal_range(std::vector <uint64_t> &, gmt_data_t,  uint64_t * columns);

/*
 * Check that the elements of gmt array are sorted in ascending order according to columns.
*/
void checkSort(std::string name, gmt_data_t data, uint64_t * columns);

/********** COMPARATOR **********/
// compare the columns of two rows
static std::function <bool(const uint64_t *, const uint64_t *)>
get_comparator(uint64_t * columns) {
  auto cmp = [columns](const uint64_t * a, const uint64_t * b) -> bool {
    for (uint64_t * col = columns; * col != ULLONG_MAX; col ++) {
      if (a[* col] < b[* col]) return true;
      else if (a[* col] > b[* col]) return false;
    }

    return false;
  };

  return cmp;
}


/********** FIND METHODS **********/
template <typename Comparator>
uint64_t lowerbound(uint64_t * keyRow, gmt_data_t data, Comparator comp) {
   uint64_t lb = ULLONG_MAX;
   gentry_t * ga  = mem_get_gentry(data);
   uint8_t * last = ga->data + ga->nbytes_loc - ga->nbytes_elem;

   uint64_t * first_elem = (uint64_t *) ga->data;
   uint64_t * last_elem = (uint64_t *) last;

   uint64_t num_cols = ga->nbytes_elem / sizeof(uint64_t);
   uint64_t first_index = ga->goffset_bytes / ga->nbytes_elem;

   if ( ! comp((uint64_t *) ga->data, keyRow) ) {        // key <= first element on this node

      if (gmt_node_id() != 0) {                                        // if node is not 0,
         std::vector <uint64_t> value(num_cols);
         gmt_get(data, first_index - 1, (void *) value.data(), 1);     // ... get last element on preceeding node
         if ( comp(value.data(), keyRow) ) lb = first_index;           // ... if < key, return first_indes
      } else lb = 0;

   } else if ( ! comp((uint64_t *) last, keyRow) ) {     // first element < key <= last element on this node
      uint8_t * first = ga->data;
      uint64_t num_elems = ga->nbytes_loc / ga->nbytes_elem;
      
      while (num_elems > 0) {
         uint64_t step = num_elems / 2;
         uint8_t * ptr = first + step * ga->nbytes_elem;

         if ( comp((uint64_t *) ptr, keyRow) ) {
            ptr += ga->nbytes_elem;
            num_elems -= step + 1;
            first = ptr;
         } else num_elems = step;
      }

      lb = first_index + (first - ga->data) / ga->nbytes_elem;
   }

   return lb;
}


template <typename Comparator>
uint64_t upperbound(uint64_t * keyRow, gmt_data_t data, Comparator comp) {
   uint64_t ub = ULLONG_MAX;
   gentry_t * ga  = mem_get_gentry(data);
   uint8_t * last = ga->data + ga->nbytes_loc - ga->nbytes_elem;

   uint64_t * first_elem = (uint64_t *) ga->data;
   uint64_t * last_elem = (uint64_t *) last;

   uint64_t num_cols = ga->nbytes_elem / sizeof(uint64_t);
   uint64_t first_index = ga->goffset_bytes / ga->nbytes_elem;

   if ( comp(keyRow, (uint64_t *) ga->data) ) {        // key < first element on this node

      if (gmt_node_id() != 0) {                                        // if node is not 0,
         std::vector <uint64_t> value(num_cols);
         gmt_get(data, first_index - 1, (void *) value.data(), 1);    // ... get last element on preceeding node
         if ( ! comp(keyRow, value.data()) ) ub = first_index;         // ... if <= key, return first_indes
      } else ub = 0;

   } else if ( comp(keyRow, (uint64_t *) last) ) {     // first element <= key < last element on this node
      uint8_t * first = ga->data;
      uint64_t num_elems = ga->nbytes_loc / ga->nbytes_elem;
      
      while (num_elems > 0) {
         uint64_t step = num_elems / 2;
         uint8_t * ptr = first + step * ga->nbytes_elem;

         if ( ! comp(keyRow, (uint64_t *) ptr) ) {
            ptr += ga->nbytes_elem;
            num_elems -= step + 1;
            first = ptr;
         } else num_elems = step;
      }

      ub = first_index + (first - ga->data) / ga->nbytes_elem;
   }

   return ub;
}

/********** SORT **********/
template <typename Comparator >
void corank_sorted(const uint64_t index, uint64_t * corank, uint64_t num_bytes,
                   gmt_data_t left,  const uint64_t leftoffset,  const uint64_t leftsize,
                   gmt_data_t right, const uint64_t rightoffset, const uint64_t rightsize, Comparator comp) {
  uint64_t delta;
  uint64_t j    = MIN(index, leftsize);
  uint64_t jlow = (index <= rightsize) ? 0 : index - rightsize;
  uint64_t k    = index - j;
  uint64_t klow = ULLONG_MAX;

  uint64_t * fromleft  = (uint64_t *) malloc(num_bytes);
  uint64_t * fromright = (uint64_t *) malloc(num_bytes);

  for ( ; ;) {
      if (j > 0 && k < rightsize) {
         gmt_get(left,  leftoffset + j - 1, (void *) fromleft,  1);
         gmt_get(right, rightoffset + k,    (void *) fromright, 1);

         if ( comp(fromright, fromleft) ) {
            delta = CEILING(j - jlow, 2);
            klow = k;
            j   -= delta;
            k   += delta;
            continue;
      }   }

      if (k > 0 && j < leftsize) {
         gmt_get(left,  leftoffset + j,      (void *) fromleft,  1);
         gmt_get(right, rightoffset + k - 1, (void *) fromright, 1);

         if ( ! comp(fromright, fromleft) ) {
            delta = CEILING(k - klow, 2);
            jlow  = j;
            j    += delta;
            k    -= delta;
            continue;
      }   }

      break;
  }

  corank[0] = j;
  corank[1] = k;
  free(fromleft);
  free(fromright);
}


template <typename Comparator >
void merge_block_section(uint64_t id, uint64_t num_coworkers, uint64_t num_bytes,
      uint64_t start, uint64_t mid, uint64_t end, gmt_data_t outdata, gmt_data_t indata, Comparator comp) {

  uint64_t i[2], lower[2], upper[2];
  i[0] = id       * (end - start) / num_coworkers;
  i[1] = (id + 1) * (end - start) / num_coworkers;

  corank_sorted(i[0], lower, num_bytes, indata, start, mid - start, indata, mid, end - mid, comp);
  corank_sorted(i[1], upper, num_bytes, indata, start, mid - start, indata, mid, end - mid, comp);

  uint64_t leftsize    = upper[0] - lower[0];
  uint64_t rightsize   = upper[1] - lower[1];

  uint64_t start_left  = start + lower[0];
  uint64_t end_left    = start_left + leftsize;
  uint64_t start_right = mid + lower[1];
  uint64_t end_right   = start_right + rightsize;

// if one side is empty, copy the other side and return
  if      (rightsize == 0) {gmt_memcpy(indata, start_left,  outdata, start + i[0], leftsize);  return; }
  else if (leftsize  == 0) {gmt_memcpy(indata, start_right, outdata, start + i[0], rightsize); return; }

// get left and right side
  uint64_t num_elems = leftsize + rightsize;
  uint8_t * buffer_in = (uint8_t *) malloc(num_elems * num_bytes);
  uint8_t * buffer_out = (uint8_t *) malloc(num_elems * num_bytes);

  uint8_t * outPtr = buffer_out;
  uint8_t * leftPtr = buffer_in;
  uint8_t * leftEnd = leftPtr + leftsize * num_bytes;
  uint8_t * rightPtr = leftEnd;
  uint8_t * rightEnd = rightPtr + rightsize * num_bytes;

  gmt_get(indata, start_left, (void *) leftPtr, leftsize);
  gmt_get(indata, start_right, (void *) rightPtr, rightsize);

// merge left and right side
  while ( (leftPtr < leftEnd) || (rightPtr < rightEnd) ) {
     if (leftPtr == leftEnd) {
        memcpy(outPtr, rightPtr, (rightEnd - rightPtr));
        rightPtr = rightEnd;
     } else if (rightPtr == rightEnd) {
        memcpy(outPtr, leftPtr, (leftEnd - leftPtr));
        leftPtr = leftEnd;
     } else if ( comp((uint64_t *) leftPtr, (uint64_t *) rightPtr) ) {
        memcpy(outPtr, leftPtr, num_bytes);
        outPtr += num_bytes; leftPtr += num_bytes;
     } else {
        memcpy(outPtr, rightPtr, num_bytes);
        outPtr += num_bytes; rightPtr += num_bytes;
  }  }

  gmt_put(outdata, start + i[0], (void *) buffer_out, num_elems);

  free(buffer_in);
  free(buffer_out);
}


/********** CHECK SORT **********/
template <typename Comparator>
void checksort(gmt_data_t data, Comparator comp) {
  gentry_t * ga = mem_get_gentry(data);
  uint64_t nbytes_elem = ga->nbytes_elem;
  uint64_t index = ga->goffset_bytes / nbytes_elem;
  uint64_t num_cols = nbytes_elem / sizeof(uint64_t);

  uint64_t * current_row = (uint64_t *) ga->data;
  uint64_t * last_row    = (uint64_t *) (ga->data + ga->nbytes_loc - nbytes_elem);

  if (gmt_node_id() != 0) {                             // if not node 0, check first element
     std::vector <uint64_t> previous_row(num_cols);
     gmt_get(data, index - 1, previous_row.data(), 1);
     if ( comp(current_row, previous_row.data()) ) printf("error: row %lu\n", index);
  }

  while (current_row < last_row) {     // check the rest of the elements
    if ( comp(current_row + num_cols, current_row) ) printf("error: row %lu\n", index);
    current_row += num_cols;
    index ++;
} }
  
#endif /* SORT_H */